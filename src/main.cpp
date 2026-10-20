#include "ReaperTrainer/ReaperGameSession.h"
#include "ReaperTrainer/TrainerConfig.h"

#include <Windows.h>
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include <MddBootstrap.h>
#include <WindowsAppSDK-VersionInfo.h>
#include <microsoft.ui.xaml.window.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/base.h>
#include "App.xaml.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ReaperTrainerUI
{
    using namespace winrt;
    using namespace winrt::Windows::Foundation;
    using namespace winrt::Microsoft::UI::Xaml;
    using namespace winrt::Microsoft::UI::Xaml::Controls;
    using namespace winrt::Microsoft::UI::Windowing;

    class BootstrapGuard
    {
    public:
        explicit BootstrapGuard(bool active) noexcept
            : active_(active)
        {
        }

        ~BootstrapGuard()
        {
            if (active_)
            {
                MddBootstrapShutdown();
            }
        }

        BootstrapGuard(const BootstrapGuard&) = delete;
        BootstrapGuard& operator=(const BootstrapGuard&) = delete;

    private:
        bool active_{};
    };

    hstring toHString(const std::string& value)
    {
        if (value.empty())
        {
            return {};
        }

        const int required = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            nullptr,
            0);
        if (required <= 0)
        {
            throw std::runtime_error("Invalid UTF-8 text.");
        }

        std::wstring wide(static_cast<std::size_t>(required), L'\0');
        if (MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                wide.data(),
                required) != required)
        {
            throw std::runtime_error("Could not convert UTF-8 text.");
        }
        return hstring(wide);
    }

    std::string formatInteger(const std::string& pattern, std::int32_t value)
    {
        std::string result = pattern;
        const std::size_t placeholder = result.find("{0}");
        if (placeholder != std::string::npos)
        {
            result.replace(placeholder, 3, std::to_string(value));
        }
        return result;
    }

    std::string formatFields(const std::string& pattern, const std::vector<std::string>& values)
    {
        std::string result = pattern;
        for (std::size_t index = 0; index < values.size(); ++index)
        {
            const std::string placeholder = "{" + std::to_string(index) + "}";
            const std::size_t position = result.find(placeholder);
            if (position != std::string::npos)
            {
                result.replace(position, placeholder.size(), values[index]);
            }
        }
        return result;
    }

    bool tryParseInteger(const std::string& text, std::int32_t& value)
    {
        const char* first = text.data();
        const char* last = first + text.size();
        while (first != last && (*first == ' ' || *first == '\t' || *first == '\r' || *first == '\n'))
        {
            ++first;
        }
        while (first != last && (last[-1] == ' ' || last[-1] == '\t' || last[-1] == '\r' || last[-1] == '\n'))
        {
            --last;
        }
        if (first == last)
        {
            return false;
        }

        const auto parsed = std::from_chars(first, last, value);
        return parsed.ec == std::errc{} && parsed.ptr == last;
    }

    std::filesystem::path executableDirectory()
    {
        std::wstring buffer(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size())
        {
            throw std::runtime_error("Could not resolve the trainer executable path.");
        }
        buffer.resize(length);
        return std::filesystem::path(buffer).parent_path();
    }

    class TrainerWindow
    {
    public:
        explicit TrainerWindow(::ReaperTrainer::TrainerConfig config)
            : config_(std::move(config)),
              activeLanguage_(config_.defaultLanguage()),
              connectionTimer_(DispatcherTimer()),
              autoCollectTimer_(DispatcherTimer()),
              appTitleText_(TextBlock()),
              languageLabel_(TextBlock()),
              gameStatusText_(TextBlock()),
              moneyTitle_(TextBlock()),
              currentMoneyText_(TextBlock()),
              skillChoicesTitle_(TextBlock()),
              currentSkillChoicesText_(TextBlock()),
              skillsTitle_(TextBlock()),
              skillPreviewText_(TextBlock()),
              logTextBlock_(TextBlock()),
              statusText_(TextBlock()),
              languageSelector_(ComboBox()),
              learnedSkillSelector_(ComboBox()),
              replacementSkillSelector_(ComboBox()),
              logScrollViewer_(ScrollViewer()),
              logBorder_(Border()),
              moneyAmountBox_(TextBox()),
              moneyAddAmountBox_(TextBox()),
              skillChoicesAmountBox_(TextBox()),
              skillChoicesAddAmountBox_(TextBox()),
              setMoneyButton_(Button()),
              addMoneyButton_(Button()),
              setSkillChoicesButton_(Button()),
              addSkillChoicesButton_(Button()),
              previewSkillButton_(Button()),
              replaceSkillButton_(Button()),
              autoCollectCheckBox_(CheckBox()),
              rootGrid_(Grid()),
              headerGrid_(Grid()),
              featureGrid_(Grid()),
              adjustmentPanel_(StackPanel()),
              moneyPanel_(StackPanel()),
              skillPanel_(StackPanel()),
              skillsPanel_(StackPanel()),
              moneyCard_(Border()),
              skillCard_(Border()),
              skillsCard_(Border()),
              statusBar_(Border())
        {
            window_ = Window();
            connectionTimer_.Interval(
                std::chrono::milliseconds(config_.processScanIntervalMilliseconds()));
            connectionTimer_.Tick([this](IInspectable const&, IInspectable const&)
            {
                onConnectionTimerTick();
            });
            autoCollectTimer_.Interval(
                std::chrono::milliseconds(config_.autoCollectIntervalMilliseconds()));
            autoCollectTimer_.Tick([this](IInspectable const&, IInspectable const&)
            {
                onAutoCollectTimerTick();
            });
            buildInterface();
            populateLanguages();
            applyLocalizedText();
            currentMoneyText_.Text(toHString(config_.text(activeLanguage_, "ui.money_unavailable")));
            currentSkillChoicesText_.Text(toHString(config_.text(activeLanguage_, "ui.locating")));
            gameStatusText_.Text(toHString(config_.text(activeLanguage_, "ui.waiting")));
            moneyAmountBox_.Text(toHString(std::to_string(config_.defaultMoneyAmount())));
            moneyAddAmountBox_.Text(toHString(std::to_string(config_.defaultMoneyAddAmount())));
            skillChoicesAmountBox_.Text(toHString(std::to_string(config_.defaultSkillChoices())));
            skillChoicesAddAmountBox_.Text(toHString(std::to_string(config_.defaultSkillChoices())));
            skillChoicesAmountBox_.IsEnabled(false);
            skillChoicesAddAmountBox_.IsEnabled(false);
            moneyAmountBox_.IsEnabled(false);
            moneyAddAmountBox_.IsEnabled(false);
            setMoneyButton_.IsEnabled(false);
            addMoneyButton_.IsEnabled(false);
            autoCollectCheckBox_.IsEnabled(false);
            setSkillChoicesButton_.IsEnabled(false);
            addSkillChoicesButton_.IsEnabled(false);
            previewSkillButton_.IsEnabled(false);
            replaceSkillButton_.IsEnabled(false);
            setStatus("ui.waiting");
        }

        ~TrainerWindow()
        {
            connectionTimer_.Stop();
            autoCollectTimer_.Stop();
            if (gameSession_.isAttached())
            {
                std::int32_t restoredCoinCount{};
                std::string errorKey;
                DWORD systemError = ERROR_SUCCESS;
                if (!gameSession_.restoreTrackedCoinDrops(restoredCoinCount, errorKey, systemError))
                {
                    const hstring message = toHString(config_.text(activeLanguage_, errorKey));
                    MessageBoxW(nullptr, message.c_str(), L"ReaperTrainer", MB_OK | MB_ICONWARNING);
                }
            }
        }

        void activate()
        {
            window_.Activate();
            configureFixedWindow();
            updateResponsiveLayout();
            appendStartupLog();
            connectionTimer_.Start();
            onConnectionTimerTick();
        }

    private:
        Border makeCard(const UIElement& content)
        {
            Border card;
            card.BorderThickness(Thickness{ 1.0, 1.0, 1.0, 1.0 });
            card.Padding(Thickness{ 12.0, 12.0, 12.0, 12.0 });
            card.Child(content);
            return card;
        }

        TextBlock makeText(const std::string& value, double fontSize = 16.0)
        {
            TextBlock text;
            text.Text(toHString(value));
            text.FontSize(fontSize);
            text.TextWrapping(TextWrapping::Wrap);
            return text;
        }

        Grid makeInputActionRow(const TextBox& input, const Button& action)
        {
            Grid row;
            row.ColumnSpacing(8.0);
            ColumnDefinition inputColumn;
            inputColumn.Width(GridLength{ 1.0, GridUnitType::Star });
            row.ColumnDefinitions().Append(inputColumn);
            ColumnDefinition actionColumn;
            actionColumn.Width(GridLength{ 1.0, GridUnitType::Auto });
            row.ColumnDefinitions().Append(actionColumn);
            input.HorizontalAlignment(HorizontalAlignment::Stretch);
            Grid::SetColumn(input, 0);
            Grid::SetColumn(action, 1);
            row.Children().Append(input);
            row.Children().Append(action);
            return row;
        }

        void buildInterface()
        {
            rootGrid_.RowSpacing(10.0);
            rootGrid_.Margin(Thickness{ 12.0, 10.0, 12.0, 10.0 });
            for (const GridLength height : {
                     GridLength{ 1.0, GridUnitType::Auto },
                     GridLength{ 3.0, GridUnitType::Star },
                     GridLength{ 1.0, GridUnitType::Star },
                     GridLength{ 1.0, GridUnitType::Auto } })
            {
                RowDefinition row;
                row.Height(height);
                rootGrid_.RowDefinitions().Append(row);
            }

            headerGrid_.ColumnSpacing(12.0);
            for (const GridLength width : {
                     GridLength{ 1.0, GridUnitType::Auto },
                     GridLength{ 1.0, GridUnitType::Auto },
                     GridLength{ 1.0, GridUnitType::Auto },
                     GridLength{ 1.0, GridUnitType::Star },
                     GridLength{ 1.0, GridUnitType::Auto } })
            {
                ColumnDefinition column;
                column.Width(width);
                headerGrid_.ColumnDefinitions().Append(column);
            }

            appTitleText_ = makeText(config_.text(activeLanguage_, "app.title"), 24.0);
            appTitleText_.VerticalAlignment(VerticalAlignment::Center);
            languageLabel_ = makeText(config_.text(activeLanguage_, "ui.language"));
            languageLabel_.VerticalAlignment(VerticalAlignment::Center);
            gameStatusText_.TextWrapping(TextWrapping::NoWrap);
            gameStatusText_.HorizontalAlignment(HorizontalAlignment::Stretch);
            gameStatusText_.VerticalAlignment(VerticalAlignment::Center);
            languageSelector_.HorizontalAlignment(HorizontalAlignment::Stretch);
            languageSelector_.VerticalAlignment(VerticalAlignment::Center);
            autoCollectCheckBox_.Content(box_value(toHString(config_.text(activeLanguage_, "ui.auto_collect"))));
            autoCollectCheckBox_.VerticalAlignment(VerticalAlignment::Center);
            Grid::SetColumn(appTitleText_, 0);
            Grid::SetColumn(languageLabel_, 1);
            Grid::SetColumn(languageSelector_, 2);
            Grid::SetColumn(gameStatusText_, 3);
            Grid::SetColumn(autoCollectCheckBox_, 4);
            headerGrid_.Children().Append(appTitleText_);
            headerGrid_.Children().Append(languageLabel_);
            headerGrid_.Children().Append(languageSelector_);
            headerGrid_.Children().Append(gameStatusText_);
            headerGrid_.Children().Append(autoCollectCheckBox_);
            Grid::SetRow(headerGrid_, 0);
            rootGrid_.Children().Append(headerGrid_);

            featureGrid_.ColumnSpacing(12.0);
            for (std::int32_t columnIndex = 0; columnIndex < 2; ++columnIndex)
            {
                ColumnDefinition column;
                column.Width(GridLength{ 1.0, GridUnitType::Star });
                featureGrid_.ColumnDefinitions().Append(column);
            }

            moneyPanel_.Orientation(Orientation::Vertical);
            moneyPanel_.VerticalAlignment(VerticalAlignment::Center);
            moneyPanel_.Spacing(10.0);
            moneyTitle_ = makeText(config_.text(activeLanguage_, "ui.money_title"), 19.0);
            currentMoneyText_.FontSize(17.0);
            setMoneyButton_.Content(box_value(toHString(config_.text(activeLanguage_, "ui.money_set"))));
            addMoneyButton_.Content(box_value(toHString(config_.text(activeLanguage_, "ui.money_add"))));
            moneyPanel_.Children().Append(moneyTitle_);
            moneyPanel_.Children().Append(currentMoneyText_);
            moneyPanel_.Children().Append(makeInputActionRow(moneyAmountBox_, setMoneyButton_));
            moneyPanel_.Children().Append(makeInputActionRow(moneyAddAmountBox_, addMoneyButton_));
            moneyCard_ = makeCard(moneyPanel_);

            skillPanel_.Orientation(Orientation::Vertical);
            skillPanel_.VerticalAlignment(VerticalAlignment::Center);
            skillPanel_.Spacing(10.0);
            skillChoicesTitle_ = makeText(config_.text(activeLanguage_, "ui.skill_title"), 19.0);
            currentSkillChoicesText_.FontSize(17.0);
            setSkillChoicesButton_.Content(box_value(toHString(config_.text(activeLanguage_, "ui.skill_choices_set"))));
            addSkillChoicesButton_.Content(box_value(toHString(config_.text(activeLanguage_, "ui.skill_choices_add"))));
            skillPanel_.Children().Append(skillChoicesTitle_);
            skillPanel_.Children().Append(currentSkillChoicesText_);
            skillPanel_.Children().Append(makeInputActionRow(skillChoicesAmountBox_, setSkillChoicesButton_));
            skillPanel_.Children().Append(makeInputActionRow(skillChoicesAddAmountBox_, addSkillChoicesButton_));
            skillCard_ = makeCard(skillPanel_);
            adjustmentPanel_.Orientation(Orientation::Vertical);
            adjustmentPanel_.VerticalAlignment(VerticalAlignment::Top);
            adjustmentPanel_.Spacing(12.0);
            adjustmentPanel_.Children().Append(moneyCard_);
            adjustmentPanel_.Children().Append(skillCard_);
            Grid::SetColumn(adjustmentPanel_, 0);
            featureGrid_.Children().Append(adjustmentPanel_);

            skillsPanel_.Orientation(Orientation::Vertical);
            skillsPanel_.VerticalAlignment(VerticalAlignment::Top);
            skillsPanel_.Spacing(6.0);
            skillsTitle_ = makeText(config_.text(activeLanguage_, "ui.skills_title"), 19.0);
            learnedSkillSelector_.PlaceholderText(
                toHString(config_.text(activeLanguage_, "ui.skill_learned_placeholder")));
            replacementSkillSelector_.PlaceholderText(
                toHString(config_.text(activeLanguage_, "ui.skill_replacement_placeholder")));
            learnedSkillSelector_.HorizontalAlignment(HorizontalAlignment::Stretch);
            replacementSkillSelector_.HorizontalAlignment(HorizontalAlignment::Stretch);
            Grid skillSelectorRow;
            skillSelectorRow.ColumnSpacing(6.0);
            for (std::int32_t columnIndex = 0; columnIndex < 2; ++columnIndex)
            {
                ColumnDefinition column;
                column.Width(GridLength{ 1.0, GridUnitType::Star });
                skillSelectorRow.ColumnDefinitions().Append(column);
            }
            Grid::SetColumn(learnedSkillSelector_, 0);
            Grid::SetColumn(replacementSkillSelector_, 1);
            skillSelectorRow.Children().Append(learnedSkillSelector_);
            skillSelectorRow.Children().Append(replacementSkillSelector_);
            StackPanel skillActionRow;
            skillActionRow.Orientation(Orientation::Horizontal);
            skillActionRow.Spacing(6.0);
            skillActionRow.HorizontalAlignment(HorizontalAlignment::Right);
            previewSkillButton_.Content(box_value(toHString(config_.text(activeLanguage_, "ui.skill_preview"))));
            replaceSkillButton_.Content(box_value(toHString(config_.text(activeLanguage_, "ui.skill_replace"))));
            skillActionRow.Children().Append(previewSkillButton_);
            skillActionRow.Children().Append(replaceSkillButton_);
            skillsPanel_.Children().Append(skillsTitle_);
            skillsPanel_.Children().Append(skillSelectorRow);
            skillsPanel_.Children().Append(skillPreviewText_);
            skillsPanel_.Children().Append(skillActionRow);
            skillsCard_ = makeCard(skillsPanel_);
            Grid::SetColumn(skillsCard_, 1);
            featureGrid_.Children().Append(skillsCard_);
            Grid::SetRow(featureGrid_, 1);
            rootGrid_.Children().Append(featureGrid_);

            logTextBlock_.TextWrapping(TextWrapping::Wrap);
            logTextBlock_.IsTextSelectionEnabled(true);
            logTextBlock_.VerticalAlignment(VerticalAlignment::Top);
            logScrollViewer_.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
            logScrollViewer_.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
            logScrollViewer_.VerticalScrollMode(ScrollMode::Enabled);
            logScrollViewer_.HorizontalScrollMode(ScrollMode::Disabled);
            logScrollViewer_.Content(logTextBlock_);
            logBorder_.BorderThickness(Thickness{ 1.0, 1.0, 1.0, 1.0 });
            logBorder_.Padding(Thickness{ 8.0, 6.0, 8.0, 6.0 });
            logBorder_.Child(logScrollViewer_);
            Grid::SetRow(logBorder_, 2);
            rootGrid_.Children().Append(logBorder_);

            statusText_.FontSize(13.0);
            statusText_.TextWrapping(TextWrapping::NoWrap);
            statusBar_.BorderThickness(Thickness{ 1.0, 1.0, 1.0, 1.0 });
            statusBar_.Padding(Thickness{ 8.0, 5.0, 8.0, 5.0 });
            statusBar_.Child(statusText_);
            Grid::SetRow(statusBar_, 3);
            rootGrid_.Children().Append(statusBar_);
            rootGrid_.SizeChanged([this](IInspectable const&, SizeChangedEventArgs const&)
            {
                updateResponsiveLayout();
            });
            window_.Content(rootGrid_);
            logScrollViewer_.Loaded([this](IInspectable const&, RoutedEventArgs const&)
            {
                scrollLogToLatest();
            });

            languageSelector_.SelectionChanged([this](IInspectable const&, SelectionChangedEventArgs const&)
            {
                onLanguageChanged();
            });
            setMoneyButton_.Click([this](IInspectable const&, RoutedEventArgs const&)
            {
                setGoldBalance();
            });
            addMoneyButton_.Click([this](IInspectable const&, RoutedEventArgs const&)
            {
                addGoldBalance();
            });
            setSkillChoicesButton_.Click([this](IInspectable const&, RoutedEventArgs const&)
            {
                setSkillChoices();
            });
            addSkillChoicesButton_.Click([this](IInspectable const&, RoutedEventArgs const&)
            {
                addSkillChoices();
            });
            learnedSkillSelector_.SelectionChanged([this](IInspectable const&, SelectionChangedEventArgs const&)
            {
                updateSkillActions();
            });
            replacementSkillSelector_.SelectionChanged([this](IInspectable const&, SelectionChangedEventArgs const&)
            {
                updateSkillActions();
            });
            previewSkillButton_.Click([this](IInspectable const&, RoutedEventArgs const&)
            {
                previewSkill();
            });
            replaceSkillButton_.Click([this](IInspectable const&, RoutedEventArgs const&)
            {
                replaceSkill();
            });
            autoCollectCheckBox_.Checked([this](IInspectable const&, RoutedEventArgs const&)
            {
                if (!settingAutoCollectCheckBox_)
                {
                    startAutoCollect();
                }
            });
            autoCollectCheckBox_.Unchecked([this](IInspectable const&, RoutedEventArgs const&)
            {
                if (!settingAutoCollectCheckBox_)
                {
                    stopAutoCollect();
                }
            });
        }

        void populateLanguages()
        {
            applyingLanguage_ = true;
            languageSelector_.Items().Clear();
            std::int32_t selectedIndex = 0;
            std::int32_t index = 0;
            for (const std::string& language : config_.languages())
            {
                ComboBoxItem item;
                item.Content(box_value(toHString(config_.text(language, "language.name"))));
                item.Tag(box_value(toHString(language)));
                languageSelector_.Items().Append(item);
                if (language == activeLanguage_)
                {
                    selectedIndex = index;
                }
                ++index;
            }
            languageSelector_.SelectedIndex(selectedIndex);
            applyingLanguage_ = false;
        }

        void applyLocalizedText()
        {
            window_.Title(L"Reaper Trainer");
            appTitleText_.Text(toHString(config_.text(activeLanguage_, "app.title")));
            languageLabel_.Text(toHString(config_.text(activeLanguage_, "ui.language")));
            moneyTitle_.Text(toHString(config_.text(activeLanguage_, "ui.money_title")));
            moneyAmountBox_.PlaceholderText(toHString(config_.text(activeLanguage_, "ui.money_amount")));
            moneyAddAmountBox_.PlaceholderText(toHString(config_.text(activeLanguage_, "ui.money_add_amount")));
            setMoneyButton_.Content(box_value(toHString(config_.text(activeLanguage_, "ui.money_set"))));
            addMoneyButton_.Content(box_value(toHString(config_.text(activeLanguage_, "ui.money_add"))));
            skillChoicesTitle_.Text(toHString(config_.text(activeLanguage_, "ui.skill_title")));
            skillChoicesAmountBox_.PlaceholderText(toHString(config_.text(activeLanguage_, "ui.skill_choices_amount")));
            skillChoicesAddAmountBox_.PlaceholderText(toHString(config_.text(activeLanguage_, "ui.skill_choices_add_amount")));
            setSkillChoicesButton_.Content(box_value(toHString(config_.text(activeLanguage_, "ui.skill_choices_set"))));
            addSkillChoicesButton_.Content(box_value(toHString(config_.text(activeLanguage_, "ui.skill_choices_add"))));
            skillsTitle_.Text(toHString(config_.text(activeLanguage_, "ui.skills_title")));
            learnedSkillSelector_.PlaceholderText(
                toHString(config_.text(activeLanguage_, "ui.skill_learned_placeholder")));
            replacementSkillSelector_.PlaceholderText(
                toHString(config_.text(activeLanguage_, "ui.skill_replacement_placeholder")));
            previewSkillButton_.Content(box_value(toHString(config_.text(activeLanguage_, "ui.skill_preview"))));
            replaceSkillButton_.Content(box_value(toHString(config_.text(activeLanguage_, "ui.skill_replace"))));
            autoCollectCheckBox_.Content(box_value(toHString(config_.text(activeLanguage_, "ui.auto_collect"))));
            gameStatusText_.Text(gameSession_.isAttached()
                ? toHString(formatInteger(
                    config_.text(activeLanguage_, "ui.attached"),
                    static_cast<std::int32_t>(gameSession_.processInfo().processId)))
                : toHString(config_.text(activeLanguage_, "ui.waiting")));
            refreshSkillList();
        }

        void onLanguageChanged()
        {
            if (applyingLanguage_)
            {
                return;
            }

            const auto item = languageSelector_.SelectedItem().try_as<ComboBoxItem>();
            if (!item)
            {
                return;
            }
            activeLanguage_ = winrt::to_string(unbox_value<hstring>(item.Tag()));
            applyLocalizedText();
            appendStartupLog();
            if (gameSession_.isAttached())
            {
                refreshGameData();
            }
        }

        void appendStartupLog()
        {
            appendLog(config_.text(activeLanguage_, "ui.money_save_note"));
            appendLog(config_.text(activeLanguage_, "ui.skill_replace_help"));
            appendLog(formatFields(
                config_.text(activeLanguage_, "ui.auto_collect_help"),
                {
                    std::to_string(config_.autoCollectIntervalMilliseconds()),
                    std::to_string(config_.autoCollectStepPixels()),
                    std::to_string(config_.autoCollectRecoveryMilliseconds())
                }));
            appendLog(config_.text(activeLanguage_, "ui.help"));
        }

        void appendLog(const std::string& message)
        {
            if (message.empty())
            {
                return;
            }

            std::string text = winrt::to_string(logTextBlock_.Text());
            text += message;
            text += "\r\n";
            constexpr std::size_t maximumLogLength = 6000;
            if (text.size() > maximumLogLength)
            {
                const std::size_t excess = text.size() - maximumLogLength;
                const std::size_t nextLine = text.find('\n', excess);
                text.erase(0, nextLine == std::string::npos ? excess : nextLine + 1);
            }

            logTextBlock_.Text(toHString(text));
            scrollLogToLatest();
        }

        void scrollLogToLatest()
        {
            if (!logScrollViewer_.Content())
            {
                return;
            }

            logScrollViewer_.UpdateLayout();
            logScrollViewer_.ScrollToVerticalOffset(logScrollViewer_.ScrollableHeight());
        }

        void configureFixedWindow()
        {
            IWindowNative* nativeWindow{};
            const HRESULT queryResult =
                reinterpret_cast<::IUnknown*>(winrt::get_abi(window_))->QueryInterface(
                    IID_PPV_ARGS(&nativeWindow));
            if (FAILED(queryResult) || nativeWindow == nullptr)
            {
                throw std::runtime_error("Could not access the ReaperTrainer window handle.");
            }

            HWND windowHandle{};
            const HRESULT handleResult = nativeWindow->get_WindowHandle(&windowHandle);
            nativeWindow->Release();
            if (FAILED(handleResult) || windowHandle == nullptr)
            {
                throw std::runtime_error("Could not retrieve the ReaperTrainer window handle.");
            }
            if (!SetWindowPos(
                    windowHandle,
                    nullptr,
                    0,
                    0,
                    1180,
                    760,
                    SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE))
            {
                throw std::runtime_error("Could not size the ReaperTrainer window.");
            }

            const auto presenter = window_.AppWindow().Presenter().try_as<OverlappedPresenter>();
            if (!presenter)
            {
                throw std::runtime_error("Could not configure a fixed-size ReaperTrainer window.");
            }
            presenter.IsResizable(false);
            presenter.IsMaximizable(false);
        }

        void updateResponsiveLayout()
        {
            const double availableWidth = rootGrid_.ActualWidth();
            const double availableHeight = rootGrid_.ActualHeight();
            if (availableWidth <= 0.0 || availableHeight <= 0.0)
            {
                return;
            }

            constexpr double designWidth = 1180.0;
            constexpr double designHeight = 700.0;
            const double scale = std::clamp(
                std::min(availableWidth / designWidth, availableHeight / designHeight),
                0.25,
                1.5);
            if (std::abs(scale - responsiveScale_) < 0.005)
            {
                return;
            }
            responsiveScale_ = scale;

            rootGrid_.RowSpacing(10.0 * scale);
            rootGrid_.Margin(Thickness{
                12.0 * scale,
                10.0 * scale,
                12.0 * scale,
                10.0 * scale
            });
            headerGrid_.ColumnSpacing(12.0 * scale);
            featureGrid_.ColumnSpacing(10.0 * scale);

            appTitleText_.FontSize(24.0 * scale);
            languageLabel_.FontSize(16.0 * scale);
            languageSelector_.Width(130.0 * scale);
            languageSelector_.FontSize(14.0 * scale);
            gameStatusText_.FontSize(14.0 * scale);
            autoCollectCheckBox_.FontSize(14.0 * scale);

            moneyCard_.Padding(Thickness{
                12.0 * scale,
                12.0 * scale,
                12.0 * scale,
                12.0 * scale
            });
            skillCard_.Padding(Thickness{
                12.0 * scale,
                12.0 * scale,
                12.0 * scale,
                12.0 * scale
            });
            skillsCard_.Padding(Thickness{
                12.0 * scale,
                12.0 * scale,
                12.0 * scale,
                12.0 * scale
            });
            moneyPanel_.Spacing(10.0 * scale);
            skillPanel_.Spacing(10.0 * scale);
            adjustmentPanel_.Spacing(12.0 * scale);
            skillsPanel_.Spacing(6.0 * scale);

            moneyTitle_.FontSize(19.0 * scale);
            currentMoneyText_.FontSize(17.0 * scale);
            skillChoicesTitle_.FontSize(19.0 * scale);
            currentSkillChoicesText_.FontSize(17.0 * scale);
            skillsTitle_.FontSize(19.0 * scale);
            learnedSkillSelector_.FontSize(14.0 * scale);
            replacementSkillSelector_.FontSize(14.0 * scale);
            skillPreviewText_.FontSize(14.0 * scale);

            const double controlHeight = 34.0 * scale;
            const double controlMinimumHeight = 28.0 * scale;
            for (const TextBox& textBox : {
                     moneyAmountBox_,
                     moneyAddAmountBox_,
                     skillChoicesAmountBox_,
                     skillChoicesAddAmountBox_ })
            {
                textBox.FontSize(14.0 * scale);
                textBox.Height(controlHeight);
                textBox.MinHeight(controlMinimumHeight);
            }
            for (const Button& button : {
                     setMoneyButton_,
                     addMoneyButton_,
                     setSkillChoicesButton_,
                     addSkillChoicesButton_,
                     previewSkillButton_,
                     replaceSkillButton_ })
            {
                button.FontSize(14.0 * scale);
                button.Height(controlHeight);
                button.MinHeight(controlMinimumHeight);
            }
            for (const ComboBox& comboBox : {
                     languageSelector_,
                     learnedSkillSelector_,
                     replacementSkillSelector_ })
            {
                comboBox.Height(controlHeight);
                comboBox.MinHeight(controlMinimumHeight);
            }

            logTextBlock_.FontSize(13.0 * scale);
            logBorder_.Padding(Thickness{
                8.0 * scale,
                6.0 * scale,
                8.0 * scale,
                6.0 * scale
            });
            statusText_.FontSize(13.0 * scale);
            statusBar_.Padding(Thickness{
                8.0 * scale,
                5.0 * scale,
                8.0 * scale,
                5.0 * scale
            });
        }

        void onConnectionTimerTick()
        {
            if (gameSession_.isAttached())
            {
                ::ReaperTrainer::PlayerEntityInfo playerEntity;
                std::string playerErrorKey;
                DWORD playerSystemError = ERROR_SUCCESS;
                if (!gameSession_.readCurrentPlayer(
                        playerEntity,
                        playerErrorKey,
                        playerSystemError))
                {
                    const bool playerWasPreviouslyDetected =
                        observedPlayerAddress_ != 0 ||
                        observedProgressAddress_ != 0 ||
                        observedTransformAddress_ != 0;
                    observedPlayerAddress_ = 0;
                    observedProgressAddress_ = 0;
                    observedTransformAddress_ = 0;
                    gameStatusText_.Text(toHString(formatInteger(
                        config_.text(activeLanguage_, "ui.waiting_for_player"),
                        static_cast<std::int32_t>(gameSession_.processInfo().processId))));
                    if (lastPlayerProbeErrorKey_ != playerErrorKey)
                    {
                        if (playerWasPreviouslyDetected || lastPlayerProbeErrorKey_.empty())
                        {
                            setStatus("ui.waiting_for_player");
                        }
                        lastPlayerProbeErrorKey_ = playerErrorKey;
                    }
                    refreshLiveValues();
                    return;
                }

                const bool playerChanged =
                    playerEntity.playerAddress != observedPlayerAddress_ ||
                    playerEntity.progressAddress != observedProgressAddress_ ||
                    playerEntity.transformAddress != observedTransformAddress_;
                observedPlayerAddress_ = playerEntity.playerAddress;
                observedProgressAddress_ = playerEntity.progressAddress;
                observedTransformAddress_ = playerEntity.transformAddress;
                lastPlayerProbeErrorKey_.clear();
                gameStatusText_.Text(toHString(formatInteger(
                    config_.text(activeLanguage_, "ui.attached"),
                    static_cast<std::int32_t>(gameSession_.processInfo().processId))));
                if (playerChanged)
                {
                    setStatus("ui.player_detected");
                    refreshGameData();
                }
                else
                {
                    refreshLiveValues();
                }
                return;
            }

            if (gameSession_.processInfo().processId != 0)
            {
                autoCollectTimer_.Stop();
                autoCollectRunning_ = false;
                gameSession_.detach();
                setAutoCollectCheckBox(false);
                setConnectionControlsEnabled(false);
                observedPlayerAddress_ = 0;
                observedProgressAddress_ = 0;
                observedTransformAddress_ = 0;
                lastPlayerProbeErrorKey_.clear();
                currentMoneyText_.Text(toHString(config_.text(activeLanguage_, "ui.no_game")));
                currentSkillChoicesText_.Text(toHString(config_.text(activeLanguage_, "ui.no_game")));
                skills_.clear();
                refreshSkillList();
                gameStatusText_.Text(toHString(config_.text(activeLanguage_, "ui.waiting")));
                setStatus("ui.game_disconnected");
            }

            connectToGame();
        }

        bool connectToGame()
        {
            std::string errorKey;
            DWORD systemError = ERROR_SUCCESS;
            if (!gameSession_.attach(config_, errorKey, systemError))
            {
                gameStatusText_.Text(toHString(
                    errorKey == "ui.no_game"
                        ? config_.text(activeLanguage_, "ui.waiting")
                        : config_.text(activeLanguage_, errorKey)));
                if (errorKey != "ui.no_game" && errorKey != lastAttachErrorKey_)
                {
                    appendLog(config_.text(activeLanguage_, errorKey));
                    if (systemError != ERROR_SUCCESS)
                    {
                        appendLog(formatInteger(
                            config_.text(activeLanguage_, "ui.error_code"),
                            static_cast<std::int32_t>(systemError)));
                    }
                }
                lastAttachErrorKey_ = errorKey;
                return false;
            }

            lastAttachErrorKey_.clear();
            observedPlayerAddress_ = 0;
            observedProgressAddress_ = 0;
            observedTransformAddress_ = 0;
            lastPlayerProbeErrorKey_.clear();
            const auto processId = static_cast<std::int32_t>(gameSession_.processInfo().processId);
            gameStatusText_.Text(toHString(formatInteger(
                config_.text(activeLanguage_, "ui.attached"),
                processId)));
            const bool canWriteToGame = gameSession_.processInfo().writable;
            setConnectionControlsEnabled(canWriteToGame);
            appendLog(formatInteger(config_.text(activeLanguage_, "ui.attached"), processId));
            refreshGameData();
            setStatus(gameSession_.processInfo().writable ? "ui.ready" : "ui.read_only");
            if (canWriteToGame && config_.autoCollectEnabledByDefault())
            {
                setAutoCollectCheckBox(true);
                startAutoCollect();
            }
            return true;
        }

        void setConnectionControlsEnabled(bool enabled)
        {
            moneyAmountBox_.IsEnabled(enabled);
            moneyAddAmountBox_.IsEnabled(enabled);
            setMoneyButton_.IsEnabled(enabled);
            addMoneyButton_.IsEnabled(enabled);
            setSkillChoicesButton_.IsEnabled(enabled);
            addSkillChoicesButton_.IsEnabled(enabled);
            skillChoicesAmountBox_.IsEnabled(enabled);
            skillChoicesAddAmountBox_.IsEnabled(enabled);
            autoCollectCheckBox_.IsEnabled(enabled);
        }

        void refreshLiveValues()
        {
            if (!gameSession_.isAttached())
            {
                return;
            }

            std::string errorKey;
            DWORD systemError = ERROR_SUCCESS;
            std::int32_t goldBalance{};
            if (gameSession_.readPersistentGold(goldBalance, errorKey, systemError))
            {
                currentMoneyText_.Text(toHString(formatInteger(
                    config_.text(activeLanguage_, "ui.current_money_value"),
                    goldBalance)));
            }
            else
            {
                currentMoneyText_.Text(toHString(config_.text(activeLanguage_, errorKey)));
            }

            std::int32_t choices{};
            if (gameSession_.readSkillChoices(choices, errorKey, systemError))
            {
                currentSkillChoicesText_.Text(toHString(formatInteger(
                    config_.text(activeLanguage_, "ui.current_skill_choices_value"),
                    choices)));
            }
            else
            {
                currentSkillChoicesText_.Text(toHString(config_.text(activeLanguage_, errorKey)));
            }
        }

        void refreshGameData()
        {
            if (!gameSession_.isAttached())
            {
                autoCollectTimer_.Stop();
                autoCollectRunning_ = false;
                setAutoCollectCheckBox(false);
                setConnectionControlsEnabled(false);
                currentMoneyText_.Text(toHString(config_.text(activeLanguage_, "ui.no_game")));
                currentSkillChoicesText_.Text(toHString(config_.text(activeLanguage_, "ui.no_game")));
                skills_.clear();
                refreshSkillList();
                return;
            }

            setConnectionControlsEnabled(gameSession_.processInfo().writable);
            refreshLiveValues();

            std::string errorKey;
            DWORD systemError = ERROR_SUCCESS;
            if (!gameSession_.readSkills(skills_, errorKey, systemError))
            {
                skills_.clear();
                appendLog(config_.text(activeLanguage_, errorKey));
            }
            else if (skills_.empty())
            {
                appendLog(config_.text(activeLanguage_, "ui.skills_empty"));
            }
            refreshSkillList();
            refreshCoinDropStatus();
        }

        void refreshCoinDropStatus()
        {
            std::vector<::ReaperTrainer::CoinDropInfo> coinDrops;
            std::int32_t sparkEntityCount{};
            std::string coinErrorKey;
            DWORD coinSystemError = ERROR_SUCCESS;
            const bool coinsReadable =
                gameSession_.readCoinDrops(coinDrops, sparkEntityCount, coinErrorKey, coinSystemError);

            if (coinsReadable)
            {
                appendLog(formatInteger(
                    config_.text(activeLanguage_, "ui.coin_drop_candidates"),
                    static_cast<std::int32_t>(coinDrops.size())) +
                    " | " +
                    formatInteger(
                    config_.text(activeLanguage_, "ui.spark_entity_candidates"),
                    sparkEntityCount));
            }
            else
            {
                appendLog(config_.text(activeLanguage_, coinErrorKey));
            }
        }

        void setAutoCollectCheckBox(bool checked)
        {
            settingAutoCollectCheckBox_ = true;
            autoCollectCheckBox_.IsChecked(checked);
            settingAutoCollectCheckBox_ = false;
        }

        void startAutoCollect()
        {
            if (!gameSession_.isAttached() || !gameSession_.processInfo().writable)
            {
                setAutoCollectCheckBox(false);
                autoCollectCheckBox_.IsEnabled(false);
                setStatus("ui.auto_collect_unavailable");
                return;
            }

            autoCollectRunning_ = true;
            setStatus("ui.auto_collect_waiting");
            autoCollectTimer_.Start();
        }

        bool stopAutoCollect()
        {
            autoCollectTimer_.Stop();
            const bool wasRunning = autoCollectRunning_;
            autoCollectRunning_ = false;

            std::int32_t restoredCoinCount{};
            std::string errorKey;
            DWORD systemError = ERROR_SUCCESS;
            if (!gameSession_.restoreTrackedCoinDrops(restoredCoinCount, errorKey, systemError))
            {
                setStatus(errorKey);
                if (systemError != ERROR_SUCCESS)
                {
                    setStatusText(formatInteger(
                        config_.text(activeLanguage_, "ui.error_code"),
                        static_cast<std::int32_t>(systemError)));
                }
                return false;
            }

            if (restoredCoinCount > 0)
            {
                setStatusText(formatInteger(
                    config_.text(activeLanguage_, "ui.auto_collect_restored"),
                    restoredCoinCount));
            }
            else if (wasRunning)
            {
                setStatus("ui.auto_collect_disabled");
            }
            return true;
        }

        void onAutoCollectTimerTick()
        {
            if (!autoCollectRunning_)
            {
                autoCollectTimer_.Stop();
                return;
            }

            ::ReaperTrainer::CoinAutoCollectStatus status;
            std::string errorKey;
            DWORD systemError = ERROR_SUCCESS;
            if (!gameSession_.moveEligibleCoinDropsTowardPlayer(
                    config_.autoCollectStepPixels(),
                    status,
                    errorKey,
                    systemError))
            {
                if (errorKey == "ui.player_data_not_ready" ||
                    errorKey == "ui.player_data_ambiguous" ||
                    errorKey == "ui.player_position_unstable" ||
                    errorKey == "ui.player_scan_failed" ||
                    errorKey == "ui.coin_scan_failed")
                {
                    statusText_.Text(toHString(config_.text(activeLanguage_, "ui.auto_collect_waiting")));
                    return;
                }

                autoCollectTimer_.Stop();
                autoCollectRunning_ = false;
                setAutoCollectCheckBox(false);

                std::int32_t restoredCoinCount{};
                std::string restoreErrorKey;
                DWORD restoreSystemError = ERROR_SUCCESS;
                if (!gameSession_.restoreTrackedCoinDrops(
                        restoredCoinCount,
                        restoreErrorKey,
                        restoreSystemError))
                {
                    errorKey = restoreErrorKey;
                    systemError = restoreSystemError;
                }

                setStatus(errorKey);
                if (systemError != ERROR_SUCCESS)
                {
                    setStatusText(formatInteger(
                        config_.text(activeLanguage_, "ui.error_code"),
                        static_cast<std::int32_t>(systemError)));
                }
                return;
            }

            statusText_.Text(toHString(formatFields(
                config_.text(activeLanguage_, "ui.auto_collect_running"),
                {
                    std::to_string(status.activeCoinCount),
                    std::to_string(status.eligibleCoinCount),
                    std::to_string(status.movedCoinCount),
                    std::to_string(status.restoredCoinCount),
                    std::to_string(status.trackedCoinCount)
                })));
        }

        std::string skillDisplayName(std::int32_t skillId) const
        {
            const std::string configuredName = config_.skillName(activeLanguage_, skillId);
            return configuredName.empty()
                ? formatInteger(config_.text(activeLanguage_, "ui.skill_id"), skillId)
                : configuredName;
        }

        void refreshSkillList()
        {
            learnedSkillSelector_.Items().Clear();
            replacementSkillSelector_.Items().Clear();
            skillPreviewText_.Text(L"");

            for (const auto& skill : skills_)
            {
                if (skill.currentLevel > 0)
                {
                    const std::string displayName =
                        skillDisplayName(skill.skillId) + " — " +
                        formatInteger(config_.text(activeLanguage_, "ui.skill_level_value"), skill.currentLevel) +
                        " / " + std::to_string(skill.maximumLevel);

                    ComboBoxItem item;
                    item.Content(box_value(toHString(displayName)));
                    item.Tag(box_value(skill.skillId));
                    learnedSkillSelector_.Items().Append(item);
                }

                if (skill.currentLevel == 0)
                {
                    ComboBoxItem item;
                    item.Content(box_value(toHString(skillDisplayName(skill.skillId))));
                    item.Tag(box_value(skill.skillId));
                    replacementSkillSelector_.Items().Append(item);
                }
            }

            updateSkillActions();
        }

        std::optional<std::int32_t> selectedSkillId(bool replacement) const
        {
            if (replacement)
            {
                const auto item = replacementSkillSelector_.SelectedItem().try_as<ComboBoxItem>();
                if (!item)
                {
                    return std::nullopt;
                }
                return unbox_value<std::int32_t>(item.Tag());
            }

            const auto item = learnedSkillSelector_.SelectedItem().try_as<ComboBoxItem>();
            if (!item)
            {
                return std::nullopt;
            }
            return unbox_value<std::int32_t>(item.Tag());
        }

        void updateSkillActions()
        {
            const bool hasSelection = selectedSkillId(false).has_value();
            previewSkillButton_.IsEnabled(hasSelection);
            replaceSkillButton_.IsEnabled(
                hasSelection &&
                selectedSkillId(true).has_value() &&
                gameSession_.isAttached() &&
                gameSession_.processInfo().writable);
        }

        void previewSkill()
        {
            const auto selected = selectedSkillId(false);
            if (!selected)
            {
                setStatus("ui.skill_select_first");
                return;
            }

            for (const auto& skill : skills_)
            {
                if (skill.skillId == *selected)
                {
                    skillPreviewText_.Text(toHString(
                        skillDisplayName(skill.skillId) +
                        " — " +
                        formatInteger(config_.text(activeLanguage_, "ui.skill_level_value"), skill.currentLevel) +
                        " / " + std::to_string(skill.maximumLevel)));
                    return;
                }
            }
        }

        void replaceSkill()
        {
            const auto current = selectedSkillId(false);
            const auto replacement = selectedSkillId(true);
            if (!current || !replacement)
            {
                setStatus("ui.skill_select_first");
                return;
            }

            std::string errorKey;
            DWORD systemError = ERROR_SUCCESS;
            if (!gameSession_.replaceSkill(*current, *replacement, errorKey, systemError))
            {
                setStatus(errorKey);
                return;
            }
            setStatus("ui.skill_replaced");
            refreshGameData();
        }

        void setSkillChoices()
        {
            std::int32_t value{};
            if (!tryParseInteger(winrt::to_string(skillChoicesAmountBox_.Text()), value))
            {
                setStatus("ui.invalid_value");
                return;
            }

            std::string errorKey;
            DWORD systemError = ERROR_SUCCESS;
            if (!gameSession_.setSkillChoices(value, errorKey, systemError))
            {
                setStatus(errorKey);
                return;
            }
            setStatus("ui.skill_choices_updated");
            refreshGameData();
        }

        void addSkillChoices()
        {
            std::int32_t amount{};
            if (!tryParseInteger(winrt::to_string(skillChoicesAddAmountBox_.Text()), amount))
            {
                setStatus("ui.invalid_value");
                return;
            }

            std::string errorKey;
            DWORD systemError = ERROR_SUCCESS;
            std::int32_t result{};
            if (!gameSession_.addSkillChoices(amount, result, errorKey, systemError))
            {
                setStatus(errorKey);
                return;
            }
            setStatus("ui.skill_choices_added");
            refreshGameData();
        }

        void setGoldBalance()
        {
            std::int32_t value{};
            if (!tryParseInteger(winrt::to_string(moneyAmountBox_.Text()), value))
            {
                setStatus("ui.invalid_value");
                return;
            }

            std::string errorKey;
            DWORD systemError = ERROR_SUCCESS;
            if (!gameSession_.setPersistentGold(value, errorKey, systemError))
            {
                setStatus(errorKey);
                return;
            }

            setStatus("ui.money_updated");
            refreshGameData();
        }

        void addGoldBalance()
        {
            std::int32_t amount{};
            if (!tryParseInteger(winrt::to_string(moneyAddAmountBox_.Text()), amount))
            {
                setStatus("ui.invalid_value");
                return;
            }

            std::string errorKey;
            DWORD systemError = ERROR_SUCCESS;
            std::int32_t result{};
            if (!gameSession_.addPersistentGold(amount, result, errorKey, systemError))
            {
                setStatus(errorKey);
                return;
            }

            currentMoneyText_.Text(toHString(formatInteger(
                config_.text(activeLanguage_, "ui.current_money_value"),
                result)));
            setStatus("ui.money_added");
            refreshGameData();
        }

        void setStatus(const std::string& key)
        {
            const std::string& message = config_.text(activeLanguage_, key);
            statusText_.Text(toHString(message));
            appendLog(message);
        }

        void setStatusText(const std::string& text)
        {
            statusText_.Text(toHString(text));
            appendLog(text);
        }

        ::ReaperTrainer::TrainerConfig config_;
        ::ReaperTrainer::ReaperGameSession gameSession_;
        std::string activeLanguage_;
        std::vector<::ReaperTrainer::SkillInfo> skills_;
        Window window_{nullptr};
        DispatcherTimer connectionTimer_{nullptr};
        DispatcherTimer autoCollectTimer_{nullptr};
        TextBlock appTitleText_{nullptr};
        TextBlock languageLabel_{nullptr};
        TextBlock gameStatusText_{nullptr};
        TextBlock moneyTitle_{nullptr};
        TextBlock currentMoneyText_{nullptr};
        TextBlock skillChoicesTitle_{nullptr};
        TextBlock currentSkillChoicesText_{nullptr};
        TextBlock skillsTitle_{nullptr};
        TextBlock skillPreviewText_{nullptr};
        TextBlock logTextBlock_{nullptr};
        TextBlock statusText_{nullptr};
        ComboBox languageSelector_{nullptr};
        ComboBox learnedSkillSelector_{nullptr};
        ComboBox replacementSkillSelector_{nullptr};
        ScrollViewer logScrollViewer_{nullptr};
        Border logBorder_{nullptr};
        TextBox moneyAmountBox_{nullptr};
        TextBox moneyAddAmountBox_{nullptr};
        TextBox skillChoicesAmountBox_{nullptr};
        TextBox skillChoicesAddAmountBox_{nullptr};
        Button setMoneyButton_{nullptr};
        Button addMoneyButton_{nullptr};
        Button setSkillChoicesButton_{nullptr};
        Button addSkillChoicesButton_{nullptr};
        Button previewSkillButton_{nullptr};
        Button replaceSkillButton_{nullptr};
        CheckBox autoCollectCheckBox_{nullptr};
        Grid rootGrid_{nullptr};
        Grid headerGrid_{nullptr};
        Grid featureGrid_{nullptr};
        StackPanel adjustmentPanel_{nullptr};
        StackPanel moneyPanel_{nullptr};
        StackPanel skillPanel_{nullptr};
        StackPanel skillsPanel_{nullptr};
        Border moneyCard_{nullptr};
        Border skillCard_{nullptr};
        Border skillsCard_{nullptr};
        Border statusBar_{nullptr};
        bool applyingLanguage_{};
        bool settingAutoCollectCheckBox_{};
        bool autoCollectRunning_{};
        std::string lastAttachErrorKey_;
        std::string lastPlayerProbeErrorKey_;
        double responsiveScale_{};
        std::uintptr_t observedPlayerAddress_{};
        std::uintptr_t observedProgressAddress_{};
        std::uintptr_t observedTransformAddress_{};
    };

    void showSystemError(const std::exception& error)
    {
        const hstring message = toHString(error.what());
        MessageBoxW(nullptr, message.c_str(), L"ReaperTrainer", MB_OK | MB_ICONERROR);
    }
}

void* winrt_make_ReaperTrainer_App()
{
    return winrt::detach_abi(
        winrt::make<winrt::ReaperTrainer::factory_implementation::App>());
}

namespace winrt::ReaperTrainer::implementation
{
    App::App()
    {
        InitializeComponent();
    }

    App::~App() = default;

    void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&)
    {
        const auto configPath = ReaperTrainerUI::executableDirectory() / L"trainer.yaml";
        auto config = ::ReaperTrainer::TrainerConfig::load(configPath);
        trainerWindow_ = std::make_unique<ReaperTrainerUI::TrainerWindow>(std::move(config));
        trainerWindow_->activate();
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    const HRESULT bootstrapResult = MddBootstrapInitialize2(
        WINDOWSAPPSDK_RELEASE_MAJORMINOR,
        WINDOWSAPPSDK_RELEASE_VERSION_TAG_W,
        PACKAGE_VERSION{ WINDOWSAPPSDK_RUNTIME_VERSION_UINT64 },
        MddBootstrapInitializeOptions_OnNoMatch_ShowUI);
    if (FAILED(bootstrapResult))
    {
        wchar_t* systemMessage = nullptr;
        const DWORD messageLength = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            static_cast<DWORD>(bootstrapResult),
            0,
            reinterpret_cast<LPWSTR>(&systemMessage),
            0,
            nullptr);
        if (messageLength != 0 && systemMessage != nullptr)
        {
            MessageBoxW(nullptr, systemMessage, L"ReaperTrainer", MB_OK | MB_ICONERROR);
            LocalFree(systemMessage);
        }
        return static_cast<int>(bootstrapResult);
    }

    ReaperTrainerUI::BootstrapGuard bootstrapGuard(true);
    try
    {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        winrt::Microsoft::UI::Xaml::Application::Start(
            [](auto&&)
            {
                winrt::make<winrt::ReaperTrainer::implementation::App>();
            });
    }
    catch (const winrt::hresult_error& error)
    {
        MessageBoxW(nullptr, error.message().c_str(), L"ReaperTrainer", MB_OK | MB_ICONERROR);
        return static_cast<int>(error.code().value);
    }
    catch (const std::exception& error)
    {
        ReaperTrainerUI::showSystemError(error);
        return 1;
    }
    return 0;
}
