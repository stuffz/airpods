#ifndef _WIN32
#include "core/key_fetch.hpp"
#include "core/monitor.hpp"
#endif
#include "core/logger.hpp"
#include "core/options.hpp"
#include "core/proximity_watch.hpp"
#include "core/stop_flag.hpp"
#include "core/tray_app.hpp"
#include "ui/app_icon.hpp"
#include "ui/dpi.hpp"

#ifdef _WIN32
#include <QByteArray>
#include <vector>

#include <windows.h>

#include <shellapi.h>

#include <cstdio>
#endif

#include <QApplication>
#include <QGuiApplication>
#include <QString>

#include <signal.h>

#include <csignal>
#include <exception>
#include <initializer_list>
#include <string>

namespace
{

bool InstallSignalHandler(int signalNumber)
{
    if (std::signal(signalNumber, core::RequestStop) == SIG_ERR)
    {
        LOG_ERROR("Failed to install handler for signal " + std::to_string(signalNumber));
        return false;
    }

    return true;
}

#ifndef _WIN32
bool RunsInTerminal(const core::Options &options)
{
    return options.console || options.once || options.list || options.ble || options.keys;
}

#endif

#ifdef _WIN32
// The binary is linked for the windows subsystem, so Explorer never creates a
// console for it and the tray comes up on its own. That leaves the console
// modes -- --ble, --help, --version, and any error on the way out -- with
// nowhere to print, so when a console already exists we borrow it.
//
// A parent console is exactly the "a shell started us" case: double-clicking
// from Explorer has none, and nothing is created here, which is the point.
void AdoptParentConsole()
{
    if (AttachConsole(ATTACH_PARENT_PROCESS) == 0)
    {
        return;
    }

    // Attaching moves the handles; the C streams have to be pointed at them
    // again or everything printed goes nowhere. Only the streams the shell did
    // not already redirect, though: a windows-subsystem process gets a null
    // standard handle when nothing is piping it, and a valid one when something
    // is, so reopening unconditionally would drag `> log.txt` back to the
    // console and quietly lose the file.
    if (GetStdHandle(STD_OUTPUT_HANDLE) == nullptr)
    {
        static_cast<void>(std::freopen("CONOUT$", "w", stdout));
    }
    if (GetStdHandle(STD_ERROR_HANDLE) == nullptr)
    {
        static_cast<void>(std::freopen("CONOUT$", "w", stderr));
    }
    if (GetStdHandle(STD_INPUT_HANDLE) == nullptr)
    {
        static_cast<void>(std::freopen("CONIN$", "r", stdin));
    }
}

#endif

} // namespace

// wWinMain rather than wmain: the windows subsystem is what keeps Explorer
// from opening a console for the tray, and -municode pairs that subsystem with
// this entry point. The command line arrives raw instead of as an argv.
#ifdef _WIN32
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
#else
int main(int argc, char **argv)
#endif
{
    try
    {
#ifdef _WIN32
        // Before anything can print.
        AdoptParentConsole();

        // The wide form throughout: the narrow CRT entry point loses characters
        // outside the Windows code page.
        int argc = 0;
        wchar_t **wideArgv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (wideArgv == nullptr)
        {
            LOG_ERROR("Cannot read the command line");
            return 1;
        }

        std::vector<QByteArray> arguments;
        arguments.reserve(argc);
        for (int i = 0; i < argc; ++i)
        {
            arguments.push_back(QString::fromWCharArray(wideArgv[i]).toUtf8());
        }
        LocalFree(wideArgv);
        std::vector<char *> pointers;
        pointers.reserve(argc + 1);
        for (auto &argument : arguments)
        {
            pointers.push_back(argument.data());
        }
        pointers.push_back(nullptr);
        char **argv = pointers.data();
#endif
        core::Options options;

        switch (core::ParseOptions(argc, argv, options))
        {
        case core::ParseResult::Exit:
            return 0;
        case core::ParseResult::Error:
            return 2;
        case core::ParseResult::Run:
            break;
        }

        Logger::Instance().SetDebugMode(options.debug);

        // Any unhandled fatal signal leaves head tracking running on the AirPods.
        // SIGPIPE counts: `airpods --debug | head` closes stdout mid-run, and the
        // default action would kill the process before the stop packet is sent.
#ifdef _WIN32
        const auto handledSignals = {SIGINT, SIGTERM};
#else
        const auto handledSignals = {SIGINT, SIGTERM, SIGHUP, SIGPIPE};
#endif
        for (const int signalNumber : handledSignals)
        {
            if (!InstallSignalHandler(signalNumber))
            {
                return 1;
            }
        }

#ifndef _WIN32
        if (options.keys)
        {
            core::KeyFetch fetch(options);
            return fetch.Run();
        }

#endif
        if (options.ble)
        {
            core::ProximityWatch watch(options);
            return watch.Run();
        }

#ifndef _WIN32
        if (RunsInTerminal(options))
        {
            core::Monitor monitor(options);
            return monitor.Run();
        }

#endif
        QApplication app(argc, argv);
        QApplication::setQuitOnLastWindowClosed(false);
        QApplication::setWindowIcon(ui::AppIcon());

        // Windows and Linux reach the same display scale by different routes;
        // without a pinned px font the window comes up a different size on each
        // at the same monitor scaling. See ui/dpi.hpp.
        ui::ApplyBaseFont();

        // Wayland gives a window no icon of its own: the compositor matches the
        // application id to a desktop file and takes the icon from there, so
        // this name has to equal the installed airpods.desktop.
        QGuiApplication::setDesktopFileName(QStringLiteral("airpods"));

        core::TrayApp tray(options);
        return tray.Run();
    }
    catch (const std::exception &ex)
    {
        LOG_ERROR(std::string("Unhandled exception: ") + ex.what());
        return 1;
    }
    catch (...)
    {
        LOG_ERROR("Unhandled unknown exception");
        return 1;
    }
}
