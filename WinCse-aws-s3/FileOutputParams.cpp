#include "FileOutputParams.hpp"

using namespace WCSE;

std::wstring FileOutputParams::str() const noexcept
{
    std::wstring sCreationDisposition;

    switch (mCreationDisposition)
    {
        case CREATE_ALWAYS:     sCreationDisposition = L"CREATE_ALWAYS";     break;
        case CREATE_NEW:        sCreationDisposition = L"CREATE_NEW";        break;
        case OPEN_ALWAYS:       sCreationDisposition = L"OPEN_ALWAYS";       break;
        case OPEN_EXISTING:     sCreationDisposition = L"OPEN_EXISTING";     break;
        case TRUNCATE_EXISTING: sCreationDisposition = L"TRUNCATE_EXISTING"; break;
        default: APP_ASSERT(0);
    }

    std::wostringstream ss;

    ss << L"mPath=" << mPath;
    ss << L" mCreationDisposition=" << sCreationDisposition;
    ss << L" mOffset=" << mOffset;
    ss << L" mLength=" << mLength;

    return ss.str();
}

// EOF