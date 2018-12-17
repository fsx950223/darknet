#include "./predict_image.h"

PredictImage::PredictImage(Detector::AsyncService* service,network* net,std::string* srv,char** names,CompletionQueue* cq,ServerCompletionQueue* server_cq,METHOD method){
    net_=net;
    srv_=srv;
    names_=names;
    cq_ = cq;
    server_cq_=server_cq;
    service_=service;
    method_=method;
    uuid_t uuid;
    uuid_generate(uuid);
    uuid_=&uuid;
    Tag<Type> *tag=new Tag<Type>(Type::DONE,uuid_);
    context_.AsyncNotifyWhenDone(static_cast<void*>(tag));
    switch(method_){
        case PREDICT_IMAGE:{
            stream_.reset(new ServerAsyncReaderWriter<DetectorReply,DetectorRequest>(&context_));
            Tag<Type> *tag=new Tag<Type>(Type::CONNECT,uuid_);
            service_->RequestPredict(&context_,stream_.get(),cq_,server_cq_,static_cast<void*>(tag));
            grpc_thread_=new std::thread(std::bind(&PredictImage::GrpcThread, this));
            break;
        }
        default:
            std::cout<<"No such method"<<std::endl;
    }
}
PredictImage::~PredictImage(){}
void PredictImage::ReadAsyncPredict(){
    Tag<Type> *tag=new Tag<Type>(Type::READ,uuid_);
    stream_->Read(&request_,static_cast<void*>(tag));
}
void PredictImage::WriteAsyncPredict(){
    if(request_.token()!="detector-grpc-token"){
        std::cout << "predict: token error" << std::endl;
        Tag<Type> *tag=new Tag<Type>(Type::DONE,uuid_);
        stream_->Finish(Status::CANCELLED,static_cast<void*>(tag));
        return;
    }
    predict_detector(request_.file(),request_.thresh(),request_.hier_thresh());
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
            Tag<Type> *tag=new Tag<Type>(Type::WRITE,uuid_);
            stream_->Write(reply,reinterpret_cast<void*>(tag));
        }
    }
}
void PredictImage::GrpcThread(){
    while(is_running_){
        if(is_busy_){
            continue;
        }
        void* tag=nullptr;
        bool ok=false;
        if(!cq_->Next(&tag,&ok)){
            std::cerr << "RPC stream closed. Quitting" << std::endl;
            break;
        };
        auto result=static_cast<Tag<Type>*>(tag);
        if(ok&&(result->uuid_==uuid_)){
            switch(Type::READ){
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
                    new PredictImage(service_,net_,srv_,names_,cq_,server_cq_,method_);
                    ReadAsyncPredict();
                    break;
                }
                case Type::DONE:{
                    std::cout << "RPC disconnecting." << std::endl;
                    grpc_thread_->join();
                    is_running_=false;
                    break;
                }
                default:{
                    std::cerr << "Unexpected tag " << tag << std::endl;
                    GPR_ASSERT(false);
                }
            }
        }
    }
    delete this;
}   
          



