#include "RecordingThread.h"
#include "HAL/RunnableThread.h"
#include <thread>
#include <mutex>
#include "TaskGraphInterfaces.h"
#include "RenderRequest.h"
#include "PIPCamera.h"


std::unique_ptr<FRecordingThread> FRecordingThread::instance_;


FRecordingThread::FRecordingThread()
    : stop_task_counter_(0), camera_(nullptr), recording_file_(nullptr), kinematics_(nullptr), is_ready_(false)
{
    thread_.reset(FRunnableThread::Create(this, TEXT("FRecordingThread"), 0, TPri_BelowNormal)); // Windows default, possible to specify more priority
}


void FRecordingThread::startRecording(msr::airlib::VehicleCameraBase* camera, const msr::airlib::Kinematics::State* kinematics, const RecordingSettings& settings, std::vector <std::string> columns, VehiclePawnWrapper* wrapper)
{
    stopRecording();

    //TODO: check FPlatformProcess::SupportsMultithreading()?

    instance_.reset(new FRecordingThread());
    instance_->camera_ = camera;
    instance_->kinematics_ = kinematics;
    instance_->settings_ = settings;
    instance_->wrapper_ = wrapper;

    instance_->last_screenshot_on_ = 0;
    instance_->last_pose_ = msr::airlib::Pose();

    instance_->is_ready_ = true;

    instance_->recording_file_.reset(new RecordingFile(columns));
    instance_->recording_file_->startRecording();
}

FRecordingThread::~FRecordingThread()
{
    stopRecording();
}

bool FRecordingThread::isRecording()
{
    return instance_ != nullptr;
}

void FRecordingThread::stopRecording()
{
    if (instance_)
    {
        instance_->EnsureCompletion();
        instance_.reset();
    }
}

/*********************** methods for instance **************************************/

bool FRecordingThread::Init()
{
    if (camera_ && recording_file_)
    {
        UAirBlueprintLib::LogMessage(TEXT("Initiated recording thread"), TEXT(""), LogDebugLevel::Success);
    }
    return true;
}

uint32 FRecordingThread::Run()
{
    while (stop_task_counter_.GetValue() == 0)
    {
        //make sire all vars are set up
        if (is_ready_) {
            bool interval_elapsed = msr::airlib::ClockFactory::get()->elapsedSince(last_screenshot_on_) > settings_.record_interval;
            bool is_pose_unequal = kinematics_ && last_pose_ != kinematics_->pose;
            if (interval_elapsed && (!settings_.record_on_move || is_pose_unequal))
            {
                last_screenshot_on_ = msr::airlib::ClockFactory::get()->nowNanos();
                last_pose_ = kinematics_->pose;

                // todo: should we go as fast as possible, or should we limit this to a particular number of
                // frames per second?
                auto response = camera_->getImage(msr::airlib::VehicleCameraBase::ImageType::Scene, false, true);
                TArray<uint8_t> image_data;
                image_data.Append(response.image_data_uint8.data(), response.image_data_uint8.size());
                recording_file_->appendRecord(image_data, wrapper_);
            }
        }
    }

    recording_file_.reset();

    return 0;
}


void FRecordingThread::Stop()
{
    stop_task_counter_.Increment();
}


void FRecordingThread::Exit()
{
    if (recording_file_)
        recording_file_.reset();
}

void FRecordingThread::EnsureCompletion()
{
    Stop();
    thread_->WaitForCompletion();
    UAirBlueprintLib::LogMessage(TEXT("Stopped recording thread"), TEXT(""), LogDebugLevel::Success);
}
