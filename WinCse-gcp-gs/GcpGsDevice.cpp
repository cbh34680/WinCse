#include "GcpGsDevice.hpp"
#include "GcpGsClient.hpp"
#include "google/cloud/internal/getenv.h"

using namespace CSELIB;

ICSDevice* NewCSDevice(PCWSTR argIniSection, NamedWorker argWorkers[])
{
    std::map<std::wstring, IWorker*> workers;

    if (NamedWorkersToMap(argWorkers, &workers) <= 0)
    {
        return nullptr;
    }

    for (const auto key: { L"delayed", L"timer", })
    {
        if (workers.find(key) == workers.cend())
        {
            return nullptr;
        }
    }

    return new CSEGGS::GcpGsDevice{ argIniSection, workers };
}

namespace CSEGGS {

CSEDVC::IApiClient* GcpGsDevice::newApiClient(CSEDVC::RuntimeEnv* argRuntimeEnv, IWorker* argDelayedWorker)
{
    return new GcpGsClient{ argRuntimeEnv, argDelayedWorker, mProjectId };
}

NTSTATUS GcpGsDevice::OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem)
{
    NEW_LOG_BLOCK();

    APP_ASSERT(argWorkDir);
    //APP_ASSERT(FileSystem);

    const auto confPath{ std::filesystem::path{ argWorkDir } / CONFIGFILE_FNAME };

    // ini ファイルから値を取得

    // DLL 種類

    std::wstring dllType;
    GetIniStringW(confPath, mIniSection, L"type", &dllType);

    if (dllType != L"gcp-gs")
    {
        errorW(L"false: DLL type mismatch dllType=%s", dllType.c_str());
        return STATUS_INVALID_PARAMETER;
    }

    // サービスアカウント認証情報

    std::wstring credentialsW;
    std::wstring projectIdW;

    GetIniStringW(confPath, mIniSection, L"credentials", &credentialsW);
    GetIniStringW(confPath, mIniSection, L"project_id",  &projectIdW);

    if (credentialsW.empty())
    {
        traceW(L"credentials empty");
    }
    else
    {
        if (!std::filesystem::is_regular_file(credentialsW))
        {
            errorW(L"file not found: credentialsW=%s", credentialsW.c_str());
            return STATUS_OBJECT_NAME_NOT_FOUND;
        }

        // JSON ファイル・パスを環境変数に登録

        _wputenv_s(L"GOOGLE_APPLICATION_CREDENTIALS", credentialsW.c_str());
        traceW(L"set GOOGLE_APPLICATION_CREDENTIALS=%s", credentialsW.c_str());
    }

    if (projectIdW.empty())
    {
        traceW(L"project_id empty");
    }
    else
    {
        // プロジェクトID を環境変数に登録

        _wputenv_s(L"GOOGLE_CLOUD_PROJECT", projectIdW.c_str());
        traceW(L"set GOOGLE_CLOUD_PROJECT=%s", projectIdW.c_str());
    }

    mProjectId = projectIdW;

    // 最後に親クラスの OnSvcStart を呼び出す

    return CSDevice::OnSvcStart(argWorkDir, FileSystem);
}

}   // namespace CSEOOS

    // EOF