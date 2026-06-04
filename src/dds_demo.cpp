#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

#include <fastdds/dds/core/condition/Condition.hpp>
#include <fastdds/dds/core/condition/StatusCondition.hpp>
#include <fastdds/dds/core/condition/WaitSet.hpp>
#include <fastdds/dds/core/Duration_t.hpp>
#include <fastdds/dds/core/status/PublicationMatchedStatus.hpp>
#include <fastdds/dds/core/status/SubscriptionMatchedStatus.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/DataWriterListener.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/qos/DataWriterQos.hpp>
#include <fastdds/dds/publisher/qos/PublisherQos.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/qos/DataReaderQos.hpp>
#include <fastdds/dds/subscriber/qos/SubscriberQos.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/dds/topic/qos/TopicQos.hpp>

#include "HelloWorld.hpp"
#include "HelloWorldPubSubTypes.hpp"

using namespace eprosima::fastdds::dds;

namespace
{

constexpr char kTopicName[] = "FastDDSMinimalHelloWorldTopic";

struct Config
{
    std::string mode;
    uint32_t samples = 5;
    uint32_t period_ms = 200;
};

Config parse_args(
        int argc,
        char** argv)
{
    if (argc < 2)
    {
        throw std::runtime_error("usage: dds_demo <pub|sub-listener|sub-waitset> [--samples N] [--period-ms N]");
    }

    Config config;
    config.mode = argv[1];

    for (int i = 2; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--samples" && i + 1 < argc)
        {
            config.samples = static_cast<uint32_t>(std::stoul(argv[++i]));
        }
        else if (arg == "--period-ms" && i + 1 < argc)
        {
            config.period_ms = static_cast<uint32_t>(std::stoul(argv[++i]));
        }
        else
        {
            throw std::runtime_error("unknown argument: " + arg);
        }
    }

    return config;
}

class DdsEntities
{
public:
    explicit DdsEntities(
            const std::string& participant_name)
        : type_(new HelloWorldPubSubType())
    {
        DomainParticipantQos participant_qos = PARTICIPANT_QOS_DEFAULT;
        participant_qos.name(participant_name);

        participant_ = DomainParticipantFactory::get_instance()->create_participant(0, participant_qos);
        if (participant_ == nullptr)
        {
            throw std::runtime_error("create_participant failed");
        }

        type_.register_type(participant_);
        topic_ = participant_->create_topic(kTopicName, type_.get_type_name(), TOPIC_QOS_DEFAULT);
        if (topic_ == nullptr)
        {
            throw std::runtime_error("create_topic failed");
        }
    }

    ~DdsEntities()
    {
        if (participant_ != nullptr)
        {
            participant_->delete_contained_entities();
            DomainParticipantFactory::get_instance()->delete_participant(participant_);
        }
    }

    DomainParticipant* participant() const
    {
        return participant_;
    }

    Topic* topic() const
    {
        return topic_;
    }

private:
    DomainParticipant* participant_ = nullptr;
    Topic* topic_ = nullptr;
    TypeSupport type_;
};

class PublisherListener : public DataWriterListener
{
public:
    void on_publication_matched(
            DataWriter*,
            const PublicationMatchedStatus& status) override
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            matched_ = status.current_count;
        }
        cv_.notify_all();
        std::cout << "Publisher matched readers: " << status.current_count << std::endl;
    }

    void wait_for_reader()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&]
                {
                    return matched_ > 0;
                });
    }

private:
    int matched_ = 0;
    std::mutex mutex_;
    std::condition_variable cv_;
};

int run_publisher(
        const Config& config)
{
    DdsEntities dds("dds_demo_publisher");
    auto* publisher = dds.participant()->create_publisher(PUBLISHER_QOS_DEFAULT);
    if (publisher == nullptr)
    {
        throw std::runtime_error("create_publisher failed");
    }

    PublisherListener listener;
    DataWriterQos writer_qos = DATAWRITER_QOS_DEFAULT;
    writer_qos.reliability().kind = RELIABLE_RELIABILITY_QOS;
    writer_qos.history().depth = 10;

    auto* writer = publisher->create_datawriter(dds.topic(), writer_qos, &listener, StatusMask::all());
    if (writer == nullptr)
    {
        throw std::runtime_error("create_datawriter failed");
    }

    listener.wait_for_reader();

    for (uint32_t i = 1; i <= config.samples; ++i)
    {
        HelloWorld sample;
        sample.index(i);
        sample.message("hello from Fast DDS");

        if (writer->write(&sample) != RETCODE_OK)
        {
            throw std::runtime_error("write failed");
        }

        std::cout << "PUB index=" << sample.index() << " message=\"" << sample.message() << "\"" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(config.period_ms));
    }

    writer->wait_for_acknowledgments(Duration_t(3, 0));
    return EXIT_SUCCESS;
}

class ListenerSubscriber : public DataReaderListener
{
public:
    explicit ListenerSubscriber(
            uint32_t expected_samples)
        : expected_samples_(expected_samples)
    {
    }

    void on_subscription_matched(
            DataReader*,
            const SubscriptionMatchedStatus& status) override
    {
        std::cout << "Listener subscriber matched writers: " << status.current_count << std::endl;
    }

    void on_data_available(
            DataReader* reader) override
    {
        HelloWorld sample;
        SampleInfo info;

        while (reader->take_next_sample(&sample, &info) == RETCODE_OK)
        {
            if (info.valid_data)
            {
                const auto count = ++received_samples_;
                std::cout << "LISTENER_SUB index=" << sample.index()
                          << " message=\"" << sample.message() << "\"" << std::endl;

                if (count >= expected_samples_)
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    done_ = true;
                    cv_.notify_all();
                }
            }
        }
    }

    void wait_done()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&]
                {
                    return done_;
                });
    }

private:
    const uint32_t expected_samples_;
    std::atomic<uint32_t> received_samples_{0};
    bool done_ = false;
    std::mutex mutex_;
    std::condition_variable cv_;
};

DataReader* create_reader(
        DdsEntities& dds,
        Subscriber*& subscriber,
        DataReaderListener* listener)
{
    subscriber = dds.participant()->create_subscriber(SUBSCRIBER_QOS_DEFAULT);
    if (subscriber == nullptr)
    {
        throw std::runtime_error("create_subscriber failed");
    }

    DataReaderQos reader_qos = DATAREADER_QOS_DEFAULT;
    reader_qos.reliability().kind = RELIABLE_RELIABILITY_QOS;
    reader_qos.history().depth = 10;

    auto* reader = subscriber->create_datareader(dds.topic(), reader_qos, listener, StatusMask::all());
    if (reader == nullptr)
    {
        throw std::runtime_error("create_datareader failed");
    }

    return reader;
}

int run_listener_subscriber(
        const Config& config)
{
    DdsEntities dds("dds_demo_listener_subscriber");
    Subscriber* subscriber = nullptr;
    ListenerSubscriber listener(config.samples);
    create_reader(dds, subscriber, &listener);

    std::cout << "Listener subscriber waiting for " << config.samples << " samples" << std::endl;
    listener.wait_done();
    return EXIT_SUCCESS;
}

int run_waitset_subscriber(
        const Config& config)
{
    DdsEntities dds("dds_demo_waitset_subscriber");
    Subscriber* subscriber = nullptr;
    DataReader* reader = create_reader(dds, subscriber, nullptr);

    auto& condition = reader->get_statuscondition();
    condition.set_enabled_statuses(StatusMask::subscription_matched() | StatusMask::data_available());

    WaitSet wait_set;
    wait_set.attach_condition(condition);

    uint32_t received_samples = 0;
    std::cout << "WaitSet subscriber blocking for " << config.samples << " samples" << std::endl;

    while (received_samples < config.samples)
    {
        ConditionSeq active_conditions;
        const ReturnCode_t ret = wait_set.wait(active_conditions, Duration_t(10, 0));
        if (ret != RETCODE_OK)
        {
            throw std::runtime_error("wait_set.wait timed out or failed");
        }

        for (Condition* active : active_conditions)
        {
            if (active != &condition)
            {
                continue;
            }

            const StatusMask changes = reader->get_status_changes();
            if (changes.is_active(StatusMask::subscription_matched()))
            {
                SubscriptionMatchedStatus status;
                reader->get_subscription_matched_status(status);
                std::cout << "WaitSet subscriber matched writers: " << status.current_count << std::endl;
            }

            if (changes.is_active(StatusMask::data_available()))
            {
                HelloWorld sample;
                SampleInfo info;
                while (reader->take_next_sample(&sample, &info) == RETCODE_OK)
                {
                    if (info.valid_data)
                    {
                        ++received_samples;
                        std::cout << "WAITSET_SUB index=" << sample.index()
                                  << " message=\"" << sample.message() << "\"" << std::endl;
                    }
                }
            }
        }
    }

    wait_set.detach_condition(condition);
    return EXIT_SUCCESS;
}

} // namespace

int main(
        int argc,
        char** argv)
{
    try
    {
        const Config config = parse_args(argc, argv);
        if (config.mode == "pub")
        {
            return run_publisher(config);
        }
        if (config.mode == "sub-listener")
        {
            return run_listener_subscriber(config);
        }
        if (config.mode == "sub-waitset")
        {
            return run_waitset_subscriber(config);
        }
        throw std::runtime_error("unknown mode: " + config.mode);
    }
    catch (const std::exception& e)
    {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}
