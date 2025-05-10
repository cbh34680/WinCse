# WinCse &middot; Windows Cloud Storage Explorer

WinCse は、AWS S3 バケットを Windows Explorer に統合するアプリケーションで、S3 バケットをローカルのファイルシステムのように扱うことができます。

## 主な機能
- Windows ファイル共有のような感覚で S3 上のファイルを操作できます。
- マウント時に表示する S3 バケットの名前や数を調整可能です。
- S3 バケットを読み取り専用モードでマウントすることができます。

## システム要件
- Windows 10 以降を推奨
- [WinFsp](http://www.secfs.net/winfsp/)
- [AWS SDK for C++](https://github.com/aws/aws-sdk-cpp)

## インストール手順
1. [WinFsp](https://winfsp.dev/rel/) をインストールする。
2. [リリースページ](https://github.com/cbh34680/WinCse/releases) から WinCse (AWS SDK for C++ を内包) をダウンロードする。

## 使用方法
1. `setup/install-aws-s3.bat` を管理者権限で実行する。
2. フォーム画面が表示されたら、AWS の認証情報を入力する。
3. **作成** ボタンを押す。
4. 表示された Explorer のディレクトリから `mount.bat` を実行する。
5. フォーム画面で選択したドライブで、Windows Explorer から S3 バケットにアクセスできるようになる。
6. `un-mount.bat` を実行すると、マウントしたドライブを解除できる。

## アンインストール方法
1. マウントしたドライブをアンマウントする。
2. `reg-del.bat` を管理者権限で実行し、WinFsp に登録されたレジストリ情報を削除する。
3. `*.bat` ファイルがあるディレクトリを削除する。
4. 必要がなければ、[WinFsp](https://winfsp.dev/rel/) をアンインストールする。

## 制限事項
- いくつかの制約については [設定ファイル](./doc/conf-example.txt) を変更することで緩和可能です。
- バケットの作成・削除は利用できません。
- 不具合の検出を容易にするため `abort()` を使用しており、強制終了する可能性があります。
- その他の制限事項については [制限事項](./doc/limitations-ja.md) を参照してください。

## 注意事項
- 本ソフトウェアは Windows 11 のみで動作確認されています。他のバージョンとの互換性は保証されていません。

## ライセンス
本プロジェクトは [GPLv3](https://www.gnu.org/licenses/gpl-3.0.html) および [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0) のもとでライセンスされています。
