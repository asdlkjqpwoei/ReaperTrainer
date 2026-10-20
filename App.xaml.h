#pragma once

#include <memory>

#include "App.g.h"

namespace ReaperTrainerUI
{
    class TrainerWindow;
}

namespace winrt::ReaperTrainer::implementation
{
    struct App : AppT<App>
    {
        App();
        ~App();

        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const& args);

    private:
        std::unique_ptr<ReaperTrainerUI::TrainerWindow> trainerWindow_;
    };
}

namespace winrt::ReaperTrainer::factory_implementation
{
    struct App : AppT<App, implementation::App>
    {
    };
}
