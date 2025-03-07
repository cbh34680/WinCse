#include <iostream>
#include <mutex>
#include <unordered_map>
#include <Windows.h>

#define APP_ASSERT      _ASSERT

//
// COPY -->
//

struct SharedBase
{
	HANDLE mLock;
	int mCount = 0;

	SharedBase()
	{
		mLock = ::CreateMutexW(NULL, FALSE, NULL);
		APP_ASSERT(mLock);
	}

	virtual ~SharedBase()
	{
		::CloseHandle(mLock);
	}
};

template<typename T, typename... Args>
T* getUnprotectedNamedDataByName(const std::wstring& name, Args... args);
void releaseUnprotectedNamedDataByName(const std::wstring& name);

template<typename T>
struct UnprotectedNamedData
{
	const std::wstring mName;
	T* mLocal = nullptr;

    template<typename... Args>
	UnprotectedNamedData(const std::wstring& argName, Args... args) : mName(argName)
	{
		mLocal = getUnprotectedNamedDataByName<T>(mName, args...);
	}

	~UnprotectedNamedData()
	{
		releaseUnprotectedNamedDataByName(mName);
	}
};
 
template<typename T>
struct ProtectedNamedData
{
	UnprotectedNamedData<T>& mUnprotectedNamedData;

	ProtectedNamedData(UnprotectedNamedData<T>& argUnprotectedNamedData)
		: mUnprotectedNamedData(argUnprotectedNamedData)
	{
		const auto reason = ::WaitForSingleObject(mUnprotectedNamedData.mLocal->mLock, INFINITE);
        APP_ASSERT(reason == WAIT_OBJECT_0);
	}

	~ProtectedNamedData()
	{
		::ReleaseMutex(mUnprotectedNamedData.mLocal->mLock);
	}

	T* operator->() {
		return mUnprotectedNamedData.mLocal;
	}

	const T* operator->() const {
		return mUnprotectedNamedData.mLocal;
	}
};

static std::mutex gGuard;
static std::unordered_map<std::wstring, std::unique_ptr<SharedBase>> gNameLocals;

template<typename T, typename... Args>
T* getUnprotectedNamedDataByName(const std::wstring& name, Args... args)
{
    std::lock_guard<std::mutex> lock_(gGuard);

    auto it{ gNameLocals.find(name) };
    if (it == gNameLocals.end())
    {
        it = gNameLocals.emplace(name, std::make_unique<T>(args...)).first;
    }

    it->second->mCount++;

    static_assert(std::is_base_of<SharedBase, T>::value, "T must be derived from SharedBase");

    T* t = dynamic_cast<T*>(it->second.get());
    APP_ASSERT(t);

    return t;
}

void releaseUnprotectedNamedDataByName(const std::wstring& name)
{
    std::lock_guard<std::mutex> lock_(gGuard);

    auto it{ gNameLocals.find(name) };

    it->second->mCount--;

    if (it->second->mCount == 0)
    {
        gNameLocals.erase(it);
    }
}

struct Shared_Simple : public SharedBase { };
struct Shared_Multipart : public SharedBase
{
    Shared_Multipart(int i)
    {
        int iii = 0;
    }
};

//
// COPY <--
//

void test3()
{
    UnprotectedNamedData<Shared_Multipart> unlockLocal(L"aaa.txt", 10);

    {
        ProtectedNamedData<Shared_Multipart> lockedLocal(unlockLocal);
    }
}

void test2_worker(int id, const std::wstring& key, int sec)
{
    UnprotectedNamedData<Shared_Simple> unlockLocal(key);

    {
        ProtectedNamedData<Shared_Simple> lockedLocal(unlockLocal);

        std::wcerr << key << L" id=" << id << L" sleep(" << sec << L") in ..." << std::endl;
        ::Sleep(1000 * sec);
        std::wcerr << key << L" id=" << id << L" sleep(" << sec << L") out" << std::endl;
    }
}

void test2()
{
    std::thread t1(test2_worker, 1, L"file1.txt", 10);
    std::thread t2(test2_worker, 2, L"file1.txt", 10);
    std::thread t3(test2_worker, 3, L"file2.txt", 0);
    std::thread t4(test2_worker, 4, L"file2.txt", 0);
    std::thread t5(test2_worker, 5, L"file3.txt", 2);
    std::thread t6(test2_worker, 6, L"file3.txt", 2);

    t1.join();
    t2.join();
    t3.join();
    t4.join();
    t5.join();
    t6.join();

    std::wcout << L"done." << std::endl;
}

int wmain(int argc, wchar_t** argv)
{
	//test1(argc, argv);

	//test2();
    test3();
}

// EOF