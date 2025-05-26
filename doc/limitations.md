# Limitations

This document explains the current limitations.

## Buckets
- **Cannot access buckets in different regions**  
  Buckets in regions different from the authentication credentials are considered inaccessible and will not be displayed.

- **Bucket creation and deletion are not available**  
  Operations such as creating or deleting buckets are not supported.

- **Hidden files**  
  Buckets in different regions are treated as hidden files and will not be visible.

## Objects
- **Handling of directories**  
  Since S3 does not support traditional directories, directory creation is simulated by creating empty placeholder objects.

- **Renaming directories**  
  Directories can only be renamed if they are empty.

- **Deleting directories**  
  By default, directories can only be deleted if they are empty.

## Explorer
- **Cannot change attributes**  
  File attributes (e.g., read-only or security settings) cannot be modified because S3 does not support traditional file attribute management.

- **Hidden files**  
  Files or directories whose names start with "." are treated as hidden files.

## General Operation
- **Use of `abort()`**  
  If an unexpected error occurs, the application may automatically terminate and unmount the drive.  
  During this process, a stack trace is recorded at `%windir%\Temp\WinCse\` for debugging purposes.

- **S3 usage costs**  
  The application executes API calls and stores files in S3 storage.  
  Be mindful of potential costs when handling large file sizes.

- **Verification status**  
  This software is personally developed, and its testing environment is limited. While extensive verification has not been conducted, improvements and fixes are continuously made.  
  Users should exercise caution when handling important data.  
  If you encounter issues, please report them via GitHub Issues.
