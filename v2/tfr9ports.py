defs = dict(
    CONSOLE_PORT  =     0xFF04,
    BLOCK_PORT    =     0xFF08,
)

if __name__ == '__main__':
    with open('n9drivers/tfr9ports.gen.d', 'w') as w:
        for k, v in sorted(defs.items()):
            print('%-20s equ $%04x' % (k, v), file=w)

    with open('tmanager/tfr9ports.gen.h', 'w') as w:
        for k, v in sorted(defs.items()):
            print('#define %-20s 0x%04x' % (k, v), file=w)

    with open('tmanager903/tfr9ports.gen.h', 'w') as w:
        for k, v in sorted(defs.items()):
            print('#define %-20s 0x%04x' % (k, int(v)), file=w)
