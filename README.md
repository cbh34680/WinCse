# WinCse &middot; Windows Cloud Storage Explorer

WinCse is an application that integrates object storage into Windows Explorer, allowing you to manage buckets as if they were local file systems.

## Main Features
- Operate object storage files just like Windows file sharing
- Adjust the names and number of displayed buckets
- Supports read-only mounting
- **Compatible with AWS S3 &middot; Google Cloud Storage &middot; OCI Object Storage**

## System Requirements
- Windows 11 or later recommended
- [WinFsp](http://www.secfs.net/winfsp/)
- Required SDKs:
  - [AWS SDK for C++](https://github.com/aws/aws-sdk-cpp)
  - [Google Cloud Platform C++ Client Libraries](https://github.com/googleapis/google-cloud-cpp)

## Installation Steps
1. Install [WinFsp](https://winfsp.dev/rel/)
2. Download WinCse from the [release page](https://github.com/cbh34680/WinCse/releases)

## Usage
1. Run the appropriate script **with administrator privileges**, depending on the storage type:
   - **AWS S3** &rarr; `setup/install-aws-s3.bat`
   - **GCP GS** &rarr; `setup/install-gcp-gs.bat`
   - **OCI OS** &rarr; `setup/install-oci-os.bat`
2. When the form screen appears, enter your authentication information and click **Create**.  
3. Run `mount.bat` from the displayed Explorer directory to mount the storage.  
4. Access the bucket files through Windows Explorer.  
5. Run `un-mount.bat` to unmount the drive.  

## Uninstallation Steps
1. Run `un-mount.bat` to unmount the drive  
2. Run `reg-del.bat` with administrator privileges  
3. Delete the directory containing `*.bat` files  
4. Uninstall **WinFsp** if no longer needed  

## Update Procedure
1. Unmount the drive  
2. Download and extract the zip file from the [release page](https://github.com/cbh34680/WinCse/releases)  
3. Overwrite `setup` and `x64` in the installation directory  

## Limitations
- Bucket creation and deletion are not supported  
- `abort()` is used for error detection, which may cause forced termination  
- For more details, refer to [Limitations](./doc/limitations-en.md)  

## Notes
- Only tested on Windows 11, compatibility with other versions is not guaranteed  
- OCI Object Storage integration is based on [OCI_AWS_CPP_SDK_S3_Examples](https://github.com/tonymarkel/OCI_AWS_CPP_SDK_S3_Examples).

## License
This project is licensed under [GPLv3](https://www.gnu.org/licenses/gpl-3.0.html) and [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0).
