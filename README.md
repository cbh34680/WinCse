# WinCse &middot; Windows Cloud Storage Explorer

WinCse is an application that integrates AWS S3 buckets into Windows Explorer, allowing S3 buckets to be treated like a local file system.

## Key Features
- Operate files on S3 as if using Windows file sharing.
- Adjust the name and number of displayed S3 buckets when mounting.
- You can mount S3 buckets in read-only mode.

## System Requirements
- Windows 10 or later (recommended)
- [WinFsp](http://www.secfs.net/winfsp/)
- [AWS SDK for C++](https://github.com/aws/aws-sdk-cpp)

## Installation Instructions
1. Install [WinFsp](https://winfsp.dev/rel/).
2. Download WinCse (which includes AWS SDK for C++) from the [release page](https://github.com/cbh34680/WinCse/releases).

## Usage
1. Run `setup/install-aws-s3.bat` as administrator.
2. When the form screen appears, enter your AWS credentials.
3. Click the **Create** button.
4. Execute `mount.bat` in the directory displayed in Explorer.
5. You can now access the S3 bucket via Windows Explorer using the selected drive.
6. Run `un-mount.bat` to unmount the drive.

## Uninstallation Instructions
1. Unmount the mounted drive.
2. Run `reg-del.bat` as administrator to remove the registry entries registered in WinFsp.
3. Delete the directory containing the `*.bat` files.
4. If no longer needed, uninstall [WinFsp](https://winfsp.dev/rel/).

## Limitations
- Some constraints can be relaxed by modifying the [configuration file](./doc/conf-example.txt).
- Bucket creation and deletion are not available.
- `abort()` is used to help detect issues, but it may cause forced termination.
- For other limitations, refer to the [Limitations](./doc/limitations.md) page.

## Notes
- This software has been tested only on Windows 11, and compatibility with other versions is not guaranteed.

## License
This project is licensed under both [GPLv3](https://www.gnu.org/licenses/gpl-3.0.html) and [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0).
