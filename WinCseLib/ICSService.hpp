#pragma once
#pragma warning(push)
#pragma warning(disable: 4100)

namespace WCSE {

struct ICSService
{
	virtual ~ICSService() = default;

	virtual NTSTATUS PreCreateFilesystem(FSP_SERVICE *Service,
		PCWSTR argWorkDir, FSP_FSCTL_VOLUME_PARAMS* VolumeParams) { return STATUS_SUCCESS; };

	virtual NTSTATUS OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem) { return STATUS_SUCCESS; };

	virtual VOID OnSvcStop() { };
};

} // namespace WCSE

#pragma warning(pop)
// EOF