import sys
import os
from datetime import datetime as dt
import itertools
import typing

def getint(cols, idx, default_value, min_value=None):
    try:
        value = int(cols[idx])

        if min_value is not None:
            if value < min_value:
                value = min_value

    except IndexError:
        value = default_value

    return value

def file_open(open_path):
    try:
        f = open(open_path, 'r+b', buffering=0)
        os.chdir(os.path.abspath(os.path.dirname(f.name)))
        return f

    except FileNotFoundError:
        try:
            f = open(open_path, 'w+b', buffering=0)
            os.chdir(os.path.abspath(os.path.dirname(f.name)))
            return f

        except Exception as ex:
            print(f'Error: {ex}')
            return None

    except Exception as ex:
        print(f'Error: {ex}')
        return None

def oper_read(f: typing.IO, cols):
    rb = getint(cols, 1, default_value=1, min_value=1)
    bins = f.read(rb)
    bins_len = len(bins)

    print(f'read length={bins_len}')

    if bins_len == 0:
        print('EOF')

    else:
        data = bins.decode('utf-8')
        if len(data) < 20:
            print(f"data='{data}'")
        else:
            print("data='{}...{}'".format(data[0:15], data[-2:]))

letters = [chr(i) for i in range(ord('A'), ord('Z') + 1)]
letters_cycle = itertools.cycle(letters)

def oper_write(f: typing.IO, cols):
    wb = getint(cols, 1, default_value=1, min_value=1)

    wchr = next(letters_cycle)
    data = wchr * wb
    print(f'write length={wb}')

    if wb < 20:
        print(f"data='{data}'")
    else:
        print("data='{}...{}'".format(data[0:15], data[-2:]))

    f.write(data.encode('utf-8'))

def main():
    f = None

    if (len(sys.argv) > 1):
        f = file_open(sys.argv[1])
        if f is not None:
            print('# cwd:  ', os.getcwd())
            print()

    while True:
        if f is None:
            print('# cwd: ', os.getcwd())
            print()
            print('File Name? ', end='', flush=True)
            file_name = sys.stdin.readline().strip()

            if len(file_name) == 0:
                file_name = None
                break

            f = file_open(file_name)
            file_name = None

            if f is not None:
                print(f'open: {f.name}')

        else:
            print('{}$ '.format(os.path.abspath(f.name)), end='', flush=True)
            cols = sys.stdin.readline().strip().split(' ')
            cols = [item.lower() for item in cols]

            try:
                match cols[0]:
                    case 'p':                   # print
                        st = os.stat(f.name)
                        print('# attrib: {}'.format(st.st_file_attributes))
                        print('# ctime:  {}  "{}"'.format(st.st_ctime_ns, dt.fromtimestamp(st.st_ctime)))
                        print('# atime:  {}  "{}"'.format(st.st_atime_ns, dt.fromtimestamp(st.st_atime)))
                        print('# mtime:  {}  "{}"'.format(st.st_mtime_ns, dt.fromtimestamp(st.st_mtime)))
                        print('# size:   {}'.format(st.st_size))
                        print('# tell:   {}'.format(f.tell()))

                    case 'q':                   # quit
                        print(f'close: {f.name}')
                        f.close()
                        f = None
                        break

                    case 'c':                   # close
                        print(f'close: {f.name}')
                        f.close()
                        f = None

                    case 'd':
                        file_name = f.name
                        print(f'close: {f.name}')
                        f.close()
                        f = None
                        os.remove(file_name)
                        print(f'delete: {file_name}')
                        file_name = None

                    case 's':                   # seek forward
                        move = getint(cols, 1, default_value=0, min_value=0)
                        cur = f.seek(move)
                        print(f'position={cur}')

                    case 'b':                   # seek back
                        move = getint(cols, 1, default_value=1)
                        cur = f.seek(move * -1, 1)
                        print(f'position={cur}')

                    case 'l':
                        move = getint(cols, 1, default_value=0, min_value=0)
                        cur = f.seek(move * -1, 2)
                        print(f'position={cur}')

                    case 'r':                   # read
                        oper_read(f, cols)

                    case 'w':                   # write
                        oper_write(f, cols)

                    case 'f':                   # flush
                        f.flush()
                        print('flush')

                    case 't':
                        pos = getint(cols, 1, default_value=-1, min_value=-1)
                        if pos >= 0:
                            new_size = f.truncate(pos)
                        else:
                            new_size = f.truncate()

                        print(f'new_size={new_size}')

                    case 'v':
                        num = getint(cols, 1, default_value=1, min_value=1)
                        for _ in range(num):
                            wchr = next(letters_cycle)

                        print(f"last letter='{wchr}'")

                    case _:
                        print(f'{cols[0]}: unknown')


            except Exception as ex:
                print(f'Error: {ex}')

        print()

    if f is not None:
        print(f'close: {f.name}')
        f.close()
        f = None

if __name__ == '__main__':
    sys.exit(main())
