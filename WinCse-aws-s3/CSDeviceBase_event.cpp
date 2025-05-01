#include "CSDevice.hpp"

using namespace CSELIB;
using namespace CSEDAS3;


void CSDeviceBase::printReport(FILE* fp)
{
    fwprintf(fp, L"[ListBucketsCache]\n");
    mQueryBucket->qbReportCache(START_CALLER fp);

    fwprintf(fp, L"[ObjectCache]\n");
    mQueryObject->qoReportCache(START_CALLER fp);
}

void CSDeviceBase::onTimer()
{
    //NEW_LOG_BLOCK();

    // TimerTask ����Ăяo����A�������̌Â����̂��폜

    const auto now{ std::chrono::system_clock::now() };

    const auto num = mQueryObject->qoDeleteOldCache(START_CALLER
        now - std::chrono::minutes(mRuntimeEnv->ObjectCacheExpiryMin));

    if (num > 0)
    {
        NEW_LOG_BLOCK();

        traceW(L"delete %d records", num);

        //traceW(L"done.");
    }
}

void CSDeviceBase::onIdle()
{
    //NEW_LOG_BLOCK();

    // IdleTask ����Ăяo����A��������t�@�C���̌Â����̂��폜

    const auto now{ std::chrono::system_clock::now() };

    // IdleTask ����Ăяo����A��������t�@�C���̌Â����̂��폜

    // �o�P�b�g�E�L���b�V���̍č쐬

    mQueryBucket->qbReload(START_CALLER
        now - std::chrono::minutes(mRuntimeEnv->BucketCacheExpiryMin));
}

bool CSDeviceBase::onNotif(const std::wstring& argNotifName)
{
    NEW_LOG_BLOCK();

    if (argNotifName == L"Global\\WinCse-util-awss3-clear-cache")
    {
        mQueryBucket->qbClearCache(START_CALLER0);
        mQueryObject->qoClearCache(START_CALLER0);

        this->onTimer();
        this->onIdle();

        traceW(L">>>>> CACHE CLEAN <<<<<");

        return true;
    }

    return false;
}

// EOF