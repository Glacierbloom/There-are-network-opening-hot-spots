// 禁用实验性协程弃用警告（代码中未使用协程，但保留该宏以防编译提醒）
#define _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS

// =====================================================
// 包含标准库与 Windows API 头文件
// =====================================================
#include <iostream>  // std::wcout, std::wcerr, std::endl 等宽字符输入输出
#include <chrono>    // std::chrono::seconds, milliseconds 等时间工具
#include <thread>    // std::this_thread::sleep_for 当前线程休眠
#include <future>    // std::async, std::future_status 异步任务与超时判断
#include <windows.h> // SetConsoleCP, SetConsoleOutputCP, CP_UTF8 等控制台编码设置
#include <io.h>      // _setmode 设置文件流模式
#include <fcntl.h>   // _O_U8TEXT 以 UTF-8 文本模式打开流
#include <conio.h>   // _kbhit, _getch

// =====================================================
// 包含 C++/WinRT 网络热点相关头文件
// =====================================================
#include <winrt/Windows.Networking.Connectivity.h>     // 网络连接配置
#include <winrt/Windows.Networking.NetworkOperators.h> // 网络运营商（热点管理）
#include <winrt/Windows.Foundation.h>                  // 基础类型如 IAsyncAction

// 引入命名空间，后续代码可省略冗长前缀
using namespace winrt;
using namespace Windows::Networking::Connectivity;
using namespace Windows::Networking::NetworkOperators;
using namespace Windows::Foundation;

// =====================================================
// 工具函数：等待热点状态稳定（不再处于“过渡中”）
// 参数：管理器 —— 已构造的热点管理器引用
// 返回：true=已稳定，false=超时仍未稳定
// =====================================================
bool 等待状态稳定(NetworkOperatorTetheringManager const &管理器)
{
    // 定义最多重试 20 次，每次间隔 500ms，共等待 10 秒
    int 重试次数 = 20;

    // 循环检查，直到次数用尽
    while (重试次数-- > 0)
    {
        // 获取当前热点的工作状态（On/Off/InTransition）
        auto 状态 = 管理器.TetheringOperationalState();

        // 如果状态不是 InTransition（过渡态），表示已稳定
        if (状态 != TetheringOperationalState::InTransition)
        {
            return true; // 返回稳定成功
        }

        // 当前仍在过渡中，让出 CPU 等待 500 毫秒再重试
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // 超过最大等待次数，返回失败（状态仍为过渡）
    return false;
}


// =====================================================
// 工具函数：等待用户按键或超时退出
// 参数：等待毫秒 —— 最大等待时间（毫秒）
// 返回：true=超时退出，false=用户按键退出
// =====================================================    
bool 等待退出(int 等待毫秒)
{
    // 记录函数开始执行的时间点和结束时间点
    auto 开始时刻 = std::chrono::steady_clock::now();
    auto 结束时刻 = 开始时刻 + std::chrono::milliseconds(等待毫秒);

    std::wcout << L"等待 " << 等待毫秒 / 1000.0 << L" 秒，或按任意键退出..." << std::endl;

    while (1)
    {

        // 条件1：检查是否有按键
        if (_kbhit())
        {             // 如果有键被按下
            _getch(); // 读取该按键（避免它留在输入缓冲区）
            std::wcout << L"\n检测到按键，程序即将退出。" << std::endl;
            return false; // 按键退出，返回 false
        }

        // 条件2：检查是否超时
        auto 当前时刻 = std::chrono::steady_clock::now();
        if (当前时刻 >= 结束时刻)
        {
            std::wcout << L"\n已超时，程序即将退出。" << std::endl;
            return true; // 超时退出，返回 true
        }

        // 让出 CPU 一小段时间，避免循环空转导致 CPU 占用过高
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
// =====================================================
// 程序入口
// =====================================================
int main()
{
    // ----- 1. 配置控制台编码为 UTF-8 -----
    {
        // 设置控制台输入代码页为 UTF-8（影响 std::cin 的编码解析）
        SetConsoleCP(CP_UTF8);
        // 设置控制台输出代码页为 UTF-8（影响 std::cout/wcout 的输出编码）
        SetConsoleOutputCP(CP_UTF8);
        // 将标准输出流设置为 UTF-8 文本模式，这样 std::wcout 写入宽字符串时会自动转成 UTF-8
        _setmode(_fileno(stdout), _O_U8TEXT);
        // 同样设置标准错误流为 UTF-8 模式，保证错误信息也正常显示
        _setmode(_fileno(stderr), _O_U8TEXT);
    }
    // ----- 2. 初始化 WinRT 运行环境 -----
    // 使用多线程公寓（MTA），避免 STA 中直接 .get() 阻塞导致死锁
    init_apartment(apartment_type::multi_threaded);

    // 输出启动提示（宽字符字符串，前缀 L 表示 wchar_t）
    std::wcout << L"程序已启动" << std::endl;

    // 整个主要逻辑用 try 包裹，统一处理 WinRT 和标准异常
    try
    {
        std::wcout << L"开始初始化热点服务..." << std::endl;

        // ----- 3. 获取当前互联网连接配置 -----
        // GetInternetConnectionProfile 返回系统当前用于上网的连接（可能是 Wi-Fi、以太网、蜂窝等）
        auto 当前网络配置 = NetworkInformation::GetInternetConnectionProfile();

        // 如果返回 nullptr，说明设备没有连接到任何网络
        if (!当前网络配置)
        {
            std::wcout << L"未检测到可用网络" << std::endl;
            return 0; // 正常退出，返回 0
        }

        // 输出当前连接的名称（例如“以太网”、“WLAN”等），便于用户确认
        // ProfileName() 返回 hstring（宽字符串），可直接送给 wcout
        std::wcout << L"当前网络：" << 当前网络配置.ProfileName() << std::endl;

        // ----- 4. 创建热点管理器 -----
        // 初始化一个空的智能指针（相当于 null）
        NetworkOperatorTetheringManager 热点管理器{nullptr};

        try
        {
            // 基于刚获取的网络配置，尝试创建对应的热点管理器
            热点管理器 = NetworkOperatorTetheringManager::CreateFromConnectionProfile(当前网络配置);
        }
        catch (hresult_error const &e)
        {
            // 如果创建过程抛出 WinRT 异常（例如没有权限、驱动不支持等），输出错误并退出
            std::wcerr << L"创建热点管理器失败: " << e.message() << std::endl;
            return 1; // 非正常退出
        }

        // 再次检查对象是否有效（CreateFromConnectionProfile 可能返回 nullptr 而不抛异常）
        if (!热点管理器)
        {
            std::wcout << L"无法创建热点管理器（可能是不支持的网络类型）" << std::endl;
            return 1;
        }

        // ----- 5. 等待热点模块状态稳定 -----
        // 调用前面定义的函数，传入管理器，检查是否能在规定时间内脱离 InTransition 状态
        if (!等待状态稳定(热点管理器))
        {
            std::wcout << L"热点状态长时间未稳定，操作中止" << std::endl;
            return 1; // 超时仍未稳定，放弃操作，避免在过渡态下出错
        }

        // ----- 6. 根据当前开关状态，决定启动还是仅提示 -----
        auto 当前状态 = 热点管理器.TetheringOperationalState();

        if (当前状态 == TetheringOperationalState::On)
        {
            // 热点已经开启
            std::wcout << L"检测到热点已经开启" << std::endl;

            // 等待 3 秒，给系统时间完成 WPA 等内部初始化（非必须，仅预留）
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
        else
        {
            // 热点处于关闭状态（或 InTransition 但已通过等待变成了稳定 Off）
            std::wcout << L"正在启动热点..." << std::endl;

            try
            {
                // 调用异步启动方法，并同步等待完成（.get() 会阻塞直到操作完成）
                热点管理器.StartTetheringAsync().get();
                std::wcout << L"热点启动成功" << std::endl;
            }
            catch (hresult_error const &e)
            {
                // 启动失败时输出详细错误信息，但不退出，继续显示最终状态
                std::wcerr << L"启动失败: " << e.message() << std::endl;
            }
        }

        // ----- 7. 输出最终的热点状态 -----
        auto 最终状态 = 热点管理器.TetheringOperationalState();
        std::wcout << L"当前热点状态: ";

        // 根据枚举值输出对应的中文/英文
        switch (最终状态)
        {
        case TetheringOperationalState::On:
            std::wcout << L"已开启"; // 已开启
            break;
        case TetheringOperationalState::Off:
            std::wcout << L"已关闭"; // 已关闭
            break;
        case TetheringOperationalState::InTransition:
            std::wcout << L"过渡中"; // 过渡中（理论上经过等待后不应出现）
            break;
        }

        std::wcout << std::endl; // 换行
    }
    catch (hresult_error const &e)
    {
        // 捕获 try 块中未处理的 WinRT 异常
        std::wcerr << L"WinRT 异常: " << e.message() << std::endl;
    }
    catch (std::exception const &e)
    {
        // 捕获标准 C++ 异常，what() 返回窄字符串，用 to_hstring 转为宽字符串安全输出
        std::wcerr << L"异常: " << winrt::to_hstring(e.what()) << std::endl;
    }

    // ----- 8. 暂停退出，防止控制台一闪而过 -----
    // 调用等待退出函数，最多等待 5 秒，或用户按任意键提前退出
    bool 是因超时退出 = 等待退出(5000);

    return 0; // 正常退出
}
