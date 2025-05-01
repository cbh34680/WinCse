#pragma once
#pragma warning(push)
#pragma warning(disable: 4100)

namespace CSELIB {

struct ICSService
{
	virtual ~ICSService() = default;

	virtual NTSTATUS PreCreateFilesystem(FSP_SERVICE* Service, PCWSTR argWorkDir, FSP_FSCTL_VOLUME_PARAMS* argVolumeParams) { return STATUS_SUCCESS; };
	virtual NTSTATUS OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem) { return STATUS_SUCCESS; };
	virtual VOID OnSvcStop() { };

	virtual std::list<std::wstring> getNotificationList() { return {}; }
	virtual bool onNotif(const std::wstring& argNotifName) { return true; }
};

} // namespace CSELIB

#pragma warning(pop)
// EOF