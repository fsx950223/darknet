#include "./predict_image.h"

PredictImage::PredictImage(Detector::AsyncService* service,network* net,std::string* srv,char** names,CompletionQueue* cq,ServerCompletionQueue* server_cq,METHOD method,std::queue<PredictImage*>* gpu_queue){
    net_=net;
    srv_=srv;
    names_=names;
    cq_ = cq;
    server_cq_=server_cq;
    service_=service;
    method_=method;
    gpu_queue_=gpu_queue;
    context_.AsyncNotifyWhenDone(reinterpret_cast<void*>(Type::DONE));
    switch(method_){
        case PREDICT_IMAGE:{
            stream_.reset(new ServerAsyncReaderWriter<DetectorReply,DetectorRequest>(&context_));
            service_->RequestPredict(&context_,stream_.get(),cq_,server_cq_,reinterpret_cast<void*>(Type::CONNECT));
            grpc_thread_=new std::thread(std::bind(&PredictImage::GrpcThread, this));
            break;
        }
        default:
            std::cout<<"No such method"<<std::endl;
    }
}
PredictImage::~PredictImage(){
    grpc_thread_->join();
}
void PredictImage::ReadAsyncPredict(){
    stream_->Read(&request_,reinterpret_cast<void*>(Type::READ));
}
void PredictImage::WriteAsyncPredict(){
    if(request_.token()!="detector-grpc-token"){
        std::cout << "predict: token error" << std::endl;
        stream_->Finish(Status::CANCELLED,reinterpret_cast<void*>(Type::DONE));
        return;
    }
    gpu_queue_->push(this);
}
void PredictImage::predict_detector(std::string file, float thresh=0.25, float hier_thresh=0.5){
    srand(2222222);
    double time;
    std::string str=*srv_+file;
    int lenOfStr = str.length();
    char* input = new char[lenOfStr];
    strcpy(input,str.c_str());
    float nms=.45;
    image im = load_image_color(input,0,0);
    image sized = letterbox_image(im, net_->w, net_->h);
    layer l = net_->layers[net_->n-1];
    float *X = sized.data;
    time=what_time_is_it_now();
    network_predict(net_, X);
    printf("%s: Predicted in %f seconds.\n", input, what_time_is_it_now()-time);
    int nboxes = 0;
    detection *dets = get_network_boxes(net_, im.w, im.h, thresh, hier_thresh, 0, 1, &nboxes);
    if (nms) do_nms_sort(dets, nboxes, l.classes, nms);
    detection_json(im, dets, nboxes, thresh, names_, l.classes);
    delete input;
    free_detections(dets, nboxes);
    free_image(im);
    free_image(sized);
}
void PredictImage::detection_json(image im, detection *dets, int num, float thresh, char **names, int classes)
{
    int i,j;
    float rate;
    for(i = 0; i < num; ++i){
        char labelstr[4096] = {0};
        int clazz = -1;
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
            
            box b = dets[i].bbox;
            int left  = (b.x-b.w/2.)*im.w;
            int right = (b.x+b.w/2.)*im.w;
            int top   = (b.y-b.h/2.)*im.h;
            int bot   = (b.y+b.h/2.)*im.h;

            if(left < 0) left = 0;
            if(right > im.w-1) right = im.w-1;
            if(top < 0) top = 0;
            if(bot > im.h-1) bot = im.h-1;
            std::string name;
            name=labelstr;
            DetectorReply reply;
            reply.set_bottom(bot);
            reply.set_left(left);
            reply.set_right(right);
            reply.set_top(top);
            reply.set_name(name);
            reply.set_rate(rate);
            stream_->Write(reply,reinterpret_cast<void*>(Type::WRITE));
        }
    }
}
void PredictImage::GrpcThread(){
    while(true){
        if(is_busy_||gpu_busy_){
            continue;
        }
  
        void* tag=nullptr;
        bool ok=false;
        if(!cq_->Next(&tag,&ok)){
            std::cerr << "RPC stream closed. Quitting" << std::endl;
            break;
        };
        
        if(ok){
            switch(static_cast<Type>(reinterpret_cast<size_t>(tag))){
                case Type::READ:{
                    WriteAsyncPredict();
                    break;
                }
                case Type::WRITE:{
                    is_busy_=true;
                    ReadAsyncPredict();
                    is_busy_=false;
                    break;
                }
                case Type::CONNECT:{
                    ReadAsyncPredict();
                    break;
                }
                case Type::DONE:{
                    std::cout << "RPC disconnecting." << std::endl;
                    break;
                }
                default:{
                    std::cerr << "Unexpected tag " << tag << std::endl;
                    GPR_ASSERT(false);
                }
            }
        }
    }
}   
          



