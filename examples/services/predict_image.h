#include <queue>
#include <memory>
#include <vector>
#include <algorithm>
#include <functional>
#include "darknet.h"
#include "../../proto/detector.grpc.pb.h"
#include "base_service.h"
using namespace std;
using grpc::Status;
using grpc::ServerAsyncReaderWriter;
using detector::DetectorRequest;
using detector::DetectorReply;
using detector::Detector;

/**
 * 图片识别类
 * 
 * 主要用于城管图像识别项目
 */
class PredictImage:protected BaseService{
    public:
        PredictImage(Detector::AsyncService* service,shared_ptr<vector<unique_ptr<network>>> nets,string* srv,char** names,CompletionQueue* cq,ServerCompletionQueue* server_cq,shared_ptr<vector<shared_ptr<queue<function<void()>>>>> gpu_queues);
        ~PredictImage();
        //void DoTask();
    private:
        DetectorRequest request_;
        void GrpcThread();
        void PredictDetector(network* net,string file, float thresh, float hier_thresh);
        void ReadAsyncPredict();
        void WriteAsyncPredict(); 
        void DetectionResponse(image im, detection *dets, int num, float thresh, char **names, int classes);
        unique_ptr<ServerAsyncReaderWriter<DetectorReply,DetectorRequest>> stream_=nullptr;
        shared_ptr<vector<unique_ptr<network>>> nets_=nullptr;        
        string* srv_=nullptr;
        char** names_=nullptr;
        shared_ptr<vector<shared_ptr<queue<function<void()>>>>> gpu_queues_=nullptr;
        Detector::AsyncService* service_=nullptr;
};