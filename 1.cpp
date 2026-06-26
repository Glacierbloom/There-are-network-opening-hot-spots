#define _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS
#include <iostream>
#include <chrono>
#include <thread>
#include <future>
#include <windows.h>
#include <io.h>
#include <fcntl.h>

// WinRT头文件
#include <winrt/Windows.Networking.Connectivity.h>
#include <winrt/Windows.Networking.NetworkOperators.h>
#include <winrt/Windows.Foundation.h>

using namespace winrt;
using namespace Windows::Networking::Connectivity;
using namespace Windows::Networking::NetworkOperators;
using namespace Windows::Foundation;

// =====================================================
// 等待热点状态稳定
// =====================================================
void 等待状态稳定(NetworkOperatorTetheringManager 管理器)
{
    // 最大等待次数
    int 重试次数 = 10;

    while (重试次数-- > 0)
    {
        // 获取当前状态
        auto 状态 =
            管理器.TetheringOperationalState();

        // 不是变化状态，表示稳定
        if (状态 != TetheringOperationalState::InTransition)
        {
            return;
        }

        // 等待500ms
        std::this_thread::sleep_for(
            std::chrono::milliseconds(500));
    }
}

// =====================================================
// 主程序
// =====================================================
int main()
{
    try
    {

        // 设置控制台输出编码为 UTF-8
        SetConsoleCP(CP_UTF8);
        SetConsoleOutputCP(CP_UTF8);
        _setmode(_fileno(stdout), _O_U8TEXT);
        _setmode(_fileno(stderr), _O_U8TEXT);
        
        // 刷新输出缓冲区
        // setvbuf(stdout, nullptr, _IONBF, 0);

        std::wcout << L"程序已启动" << std::endl;

        // 初始化 WinRT
        init_apartment();

        std::wcout << L"开始初始化热点服务..."  << std::endl;

        // =================================================
        // 获取当前互联网连接
        // =================================================

        auto 当前网络配置 =
            NetworkInformation::GetInternetConnectionProfile();

        if (!当前网络配置)
        {
            std::wcout
                << L"未检测到可用网络"
                << std::endl;

            return 0;
        }

        auto 网络名称 = 当前网络配置.ProfileName();
        std::wcout << L"当前网络：" << 网络名称.c_str() << std::endl;

        // =================================================
        // 创建热点管理器
        // =================================================

        NetworkOperatorTetheringManager 热点管理器{nullptr};

        try
        {

            热点管理器 =
                NetworkOperatorTetheringManager::
                    CreateFromConnectionProfile(
                        当前网络配置);
        }
        catch (hresult_error const &e)
        {

            std::wcerr
                << L"创建热点管理器失败:"
                << e.message().c_str()
                << std::endl;
        }

        if (!热点管理器)
        {
            std::wcout
                << L"无法创建热点管理器"
                << std::endl;

            return 0;
        }

        // =================================================
        // 等待状态稳定
        // =================================================

        等待状态稳定(
            热点管理器);

        // =================================================
        // 判断热点是否已经开启
        // =================================================

        if (
            热点管理器.TetheringOperationalState() ==
            TetheringOperationalState::On)
        {

            std::wcout
                << L"检测到热点已经开启"
                << std::endl;

            // 等待系统完成WPA初始化
            std::this_thread::sleep_for(
                std::chrono::seconds(3));

            return 0;
        }

        // =================================================
        // 启动热点
        // =================================================

        std::wcout
            << L"正在启动热点..."
            << std::endl;

        try
        {

            // C++/WinRT异步转同步
            auto 异步任务 =
                热点管理器
                    .StartTetheringAsync();

            // 等待完成
            异步任务.get();

            std::wcout
                << L"热点启动成功"
                << std::endl;
        }
        catch (hresult_error const &e)
        {

            std::wcerr
                << L"启动失败:"
                << e.message().c_str()
                << std::endl;
        }

        // =================================================
        // 输出最终状态
        // =================================================

        auto 最终状态 =
            热点管理器
                .TetheringOperationalState();

        std::wcout
            << L"当前热点状态:";

        switch (最终状态)
        {

        case TetheringOperationalState::On:
            std::wcout << L"ON";
            break;

        case TetheringOperationalState::Off:
            std::wcout << L"OFF";
            break;

        case TetheringOperationalState::InTransition:
            std::wcout << L"TRANSITION";
            break;
        }

        std::wcout << std::endl;
    }
    catch (hresult_error const &e)
    {

        std::wcerr
            << L"WinRT异常:"
            << e.message().c_str()
            << std::endl;
    }
    catch (std::exception const &e)
    {

        std::wcerr
            << L"异常:"
            << std::wstring(e.what(), e.what() + std::strlen(e.what())).c_str()
            << std::endl;
    }

    // =================================================
    // 退出等待
    // =================================================

    std::wcout
        << L"按回车退出，5秒自动退出..."
        << std::endl;

    auto 等待输入 =
        std::async(
            std::launch::async,
            []
            {
                std::string s;
                std::getline(
                    std::cin,
                    s);
            });

    if (
        等待输入.wait_for(
            std::chrono::seconds(5)) ==
        std::future_status::timeout)
    {

        std::wcout
            << L"超时退出"
            << std::endl;
    }

    std::wcout
        << L"程序退出"
        << std::endl;

    return 0;
}