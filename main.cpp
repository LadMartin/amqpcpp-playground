#include <uv.h>
#include <amqpcpp.h>
#include <amqpcpp/libuv.h>
#include <csignal>

/*
The crutial part seems to be when someone the consume is called
Then for some reason the Deferred stays in the channel's list untill the consume stops
I cannot tell if this is an intended behavior, but it's not documented at least
top -o %MEM -d 1
*/

#define TAG "just-a-tag"
#define USE_BIND
// #define FEEL_FREE_TO_BRAKE_THE_MEMORY

// A helper class to track lifetime
class A
{
    public:
    A(int i) : i(i) { std::cout << "constructed A: " << i << std::endl; }
    A(const A& rhs) { i = rhs.i; std::cout << "copy constructor A: " << i << std::endl; }
    A(A&& rhs) { i = rhs.i; rhs.i = -1; std::cout << "move constructor A: " << i << std::endl; }
    ~A(){ std::cout << "destructor A: " << i << std::endl; }
    A& operator=(A rhs) { i = rhs.i; std::cout << "copy assigment A: " << i << std::endl; return *this; }
    A& operator=(A&& rhs) { i = rhs.i; rhs.i = -1; std::cout << "move assigment A: " << i << std::endl; return *this; }

    int i;
};

AMQP::Deferred& bindQueue(AMQP::TcpChannel& channel)
{
    static int count = 0;
    count++;

    std::cout << "action begin: " << count << std::endl;

#ifdef USE_BIND
    auto& foo = channel.bindQueue("amq.rabbitmq.event", "", "queue.created");
#elif
    channel.startTransaction();
    channel.publish("amq.topic", "bordel", "somemessage");
    auto& foo = channel.commitTransaction();
#endif

    foo.onSuccess([=, &channel]()
    {
        std::cout << "success declare: " << count << std::endl;
#ifdef FEEL_FREE_TO_BRAKE_THE_MEMORY
        bindQueue(channel);
#endif
    });



    constexpr std::size_t size = 10000000;
    foo.onError([
        // y = std::shared_ptr<int[]>([&]{auto bar = new int[size]; memset(bar, 0, size); bar[0] = count; return bar;}(), [](auto p){std::cout << "array deleted " << *p << std::endl; delete[] p;}),
        foo = std::make_shared<A>(count)
        // bar = A{count}
    ](auto msg)
    {
        std::cout << "error " << msg;
    });

    std::cout << "action is end: " << count << std::endl;

    return foo;
}

int main()
{
    auto *loop = uv_default_loop();
    AMQP::LibUvHandler handler(loop);
    AMQP::TcpConnection connection(&handler, AMQP::Address("amqp://guest:guest@localhost/"));
    AMQP::TcpChannel channel(&connection);

    auto signal = uv_signal_t{};
    uv_signal_init(loop, &signal);
    loop->data = &channel;

    channel.onError([&](auto reason)
    {
        std::cout << "reason: " << reason << std::endl;
        uv_stop(loop);
    });

    uv_signal_start(&signal, [](uv_signal_t* handle, int)
    {
        auto* channel = reinterpret_cast<AMQP::TcpChannel*>(handle->loop->data);
        static int foo = 0;
        foo++;
        if (foo == 4) // exit on second interrupt
        {
            // uv_stop(handle->loop);
            // std::exit(0);
            puts("closing channel");
            channel->close();
            return;
        }
        // else if (foo == 5)
        // {
        //     puts("closing connection");
        //     channel->close();
        //     return;
        // }
        else if (foo > 6) // exit on second interrupt
        {
            uv_stop(handle->loop);
            return;
        }
        
        // std::cout << "Cancel" << std::endl;
        // channel->cancel(TAG);
        bindQueue(*channel);

    }, SIGINT);

    channel.declareQueue(AMQP::exclusive);
    channel.consume("", TAG, AMQP::noack);

    bindQueue(channel);
    uv_run(loop, UV_RUN_DEFAULT);
    return 0;
}