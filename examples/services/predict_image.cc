#include "predict_image.h"
PredictImage::PredictImage(Detector::AsyncService* service,shared_ptr<vector<unique_ptr<network>>> nets,string* srv,char** names,CompletionQueue* cq,ServerCompletionQueue* server_cq,shared_ptr<vector<shared_ptr<queue<function<void()>>>>> gpu_queues){
    nets_=nets;
    srv_=srv;
    names_=names;
    cq_ = cq;
    server_cq_=server_cq;
    service_=service;
    gpu_queues_=gpu_queues;
    context_.AsyncNotifyWhenDone(reinterpret_cast<void*>(Type::DONE));
    stream_=make_unique<ServerAsyncReaderWriter<DetectorReply,DetectorRequest>>(&context_);
    service_->RequestPredict(&context_,stream_.get(),cq_,server_cq_,reinterpret_cast<void*>(Type::CONNECT));
    grpc_thread_=make_unique<thread>(bind(&PredictImage::GrpcThread, this));
}
PredictImage::~PredictImage(){
    grpc_thread_->join();
}
/** Do a task in thread */
// void PredictImage::DoTask(){
//     auto lambda=[this](){
//         PredictDetector((*nets_)[gpu_index_].get(),request_.file(),request_.thresh(),request_.hier_thresh());
//     };
// }
void PredictImage::ReadAsyncPredict(){
    stream_->Read(&request_,reinterpret_cast<void*>(Type::READ));
}

void PredictImage::WriteAsyncPredict(){
    if(request_.token()!="detector-grpc-token"){
        cout << "predict: token error" << endl;
        stream_->Finish(Status::CANCELLED,reinterpret_cast<void*>(Type::DONE));
        return;
    }
    auto iter=min_element(gpu_queues_->begin(),gpu_queues_->end(),[](shared_ptr<queue<function<void()>>> a, shared_ptr<queue<function<void()>>> b){
        return a->size()<b->size();
    });
    auto gpu_index=distance(gpu_queues_->begin(),iter);
    auto lambda=[this,gpu_index](){
        PredictDetector((*nets_)[gpu_index].get(),request_.file(),request_.thresh(),request_.hier_thresh());
    };
    (*gpu_queues_)[gpu_index]->push(lambda);
}
void PredictImage::PredictDetector(network* net,string file, float thresh=0.25, float hier_thresh=0.5){
    srand(2222222);
    double time;
    auto input =const_cast<char*>((*srv_+file).c_str());
    auto nms=.45;
    auto im = load_image_color(input,0,0);
    auto sized = letterbox_image(im, net->w, net->h);
    auto l = net->layers[net->n-1];
    auto X = sized.data;
    time=what_time_is_it_now();
    network_predict(net, X);
    printf("%s: Predicted in %f seconds.\n", input, what_time_is_it_now()-time);
    auto nboxes = 0;
    auto dets = get_network_boxes(net, im.w, im.h, thresh, hier_thresh, 0, 1, &nboxes);
    if (nms) do_nms_sort(dets, nboxes, l.classes, nms);
    DetectionResponse(im, dets, nboxes, thresh, names_, l.classes);
    free_detections(dets, nboxes);
    free_image(im);
    free_image(sized);
}
void PredictImage::DetectionResponse(image im, detection *dets, int num, float thresh, char **names, int classes)
{
    int i,j;
    float rate;
    for(i = 0; i < num; ++i){
        char labelstr[4096] = {0};
        auto clazz = -1;
        for(j = 0; j < classes; ++j){
            if (dets[i].prob[j] > thresh){
                if (clazz < 0) {
                    strcat(labelstr, names[j]);
                    clazz = j;
                } else {
                    strcat(labelstr, ", ");
                    strcat(labelstr, names[j]);
                }
                printf("%s: %.0f%%\n", names[j], dets[i].prob[j]*100);
                rate=dets[i].prob[j]*100;
            }
        }
        if(clazz >= 0){
            auto b = dets[i].bbox;
            auto left  = (b.x-b.w/2.)*im.w;
            auto right = (b.x+b.w/2.)*im.w;
            auto top   = (b.y-b.h/2.)*im.h;
            auto bot   = (b.y+b.h/2.)*im.h;
            if(left < 0) left = 0;
            if(right > im.w-1) right = im.w-1;
            if(top < 0) top = 0;
            if(bot > im.h-1) bot = im.h-1;
            DetectorReply reply;
            reply.set_bottom(bot);
            reply.set_left(left);
            reply.set_right(right);
            reply.set_top(top);
            reply.set_name(static_cast<string>(labelstr));
            reply.set_rate(rate);
            stream_->Write(move(reply),reinterpret_cast<void*>(Type::NOTHING));
        }
    }
    stream_->Finish(Status::OK,reinterpret_cast<void*>(Type::WRITE));
}
void PredictImage::GrpcThread(){
    while(true){
        // if(is_busy_){
        //     continue;
        // }
  
        void* tag=nullptr;
        bool ok=false;
        if(!cq_->Next(&tag,&ok)){
            cerr << "RPC stream closed. Quitting" << endl;
            break;
        };
        
        if(ok){
            switch(static_cast<Type>(reinterpret_cast<size_t>(tag))){
                case Type::READ:{
                    WriteAsyncPredict();
                    break;
                }
                case Type::WRITE:{
                    //is_busy_=true;
                    ReadAsyncPredict();
                    //is_busy_=false;
                    break;
                }
                case Type::CONNECT:{
                    ReadAsyncPredict();
                    break;
                }
                case Type::DONE:{
                    cout << "RPC disconnecting." << endl;
                    break;
                }
                case Type::NOTHING:{
                    break;
                }
                default:{
                    cerr << "Unexpected tag " << tag << endl;
                    GPR_ASSERT(false);
                }
            }
        }
    }
}   
          



