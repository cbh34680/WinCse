# WinCse &middot; Windows Cloud Storage Explorer

WinCse is an application that integrates AWS S3 buckets into Windows Explorer, allowing users to handle S3 buckets as if they were part of the local file system.

## Features
- Display S3 buckets in Windows Explorer
- Simple interface for easy file management

## System Requirements
- Windows 10 or later
- [WinFsp](http://www.secfs.net/winfsp/)
- [AWS SDK for C++](https://github.com/aws/aws-sdk-cpp)

## Installation
1. Install [WinFsp](https://winfsp.dev/rel/).
2. Download WinCse (including AWS SDK for C++) from the [release page](https://github.com/cbh34680/WinCse/releases).

## Usage
1. Run `setup/install-aws-s3.bat` with administrator privileges.
2. Enter your AWS credentials in the form that appears.
3. Click the `Create` button.
4. Run `mount.bat` from the displayed Explorer directory.
5. You can now access your S3 bucket as a drive selected in the form.
6. To unmount the drive, run `un-mount.bat`.

## Uninstallation
1. Run `reg-del.bat` with administrator privileges to remove the registry entries registered by WinFsp.
2. Delete the directory containing the `*.bat` files.
3. If no longer needed, uninstall [WinFsp](https://winfsp.dev/rel/).

## Limitations
- Some restrictions can be relaxed by modifying [config](./doc/conf-example.txt).
- See [limitations.md](./limitations.md) for further details.

## Notes
- The current version is in testing.
- Verified to work only on Windows 11.
- If you need a stable alternative, consider using [Rclone](https://rclone.org/).

## License
This project is licensed under both [GPLv3](https://www.gnu.org/licenses/gpl-3.0.html) and [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0).
