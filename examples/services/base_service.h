#include <unistd.h>
#include <grpcpp/grpcpp.h>
#include <thread>
using grpc::ServerCompletionQueue;
using grpc::CompletionQueue;
using grpc::ServerContext;

class BaseService{
    protected: 
        CompletionQueue* cq_=nullptr;
        ServerCompletionQueue* server_cq_=nullptr;
        ServerContext context_;
        bool is_busy_=false;
        enum Type { READ=1, WRITE=2, CONNECT=3,DONE=4, FINISH=5,NOTHING=6 };
        std::unique_ptr<std::thread> grpc_thread_=nullptr;
};
// template <class T>
// class Tag{
//     public:
//         Tag<T>(T type,uuid_t* uuid){
//             type_=type;
//             uuid_=uuid;
//         };
//         ~Tag(){};
//         T type_;
//         uuid_t* uuid_;
// };