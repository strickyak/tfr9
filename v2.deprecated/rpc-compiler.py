import sys, os

def MustLen(want, words):
    if want != len(words):
        raise Exception("Expected %s words on line, but got these: %s", want, repr(words))

def MustBe(want, got):
    if want != got:
        raise Exception("Expected %s but got %s", want, got)

class RPC:
    def __init__(self, name, num):
        self.name = name
        self.num = num
        self.ins = []
        self.outs = []
        self.errs = []


def Process(definitions, hfile, gofile):
        lines = [line.rstrip() for line in open(definitions)]
        w_cc = open(hfile, 'w')
        w_go = open(gofile, 'w')
        rpcs = dict()

        current = None
    
        def cc(format, *args):
            print(format, *args, file=w_cc)
        def go(format, *args):
            print(format, *args, file=w_go)

        #############################################    def start(self):

        for line in lines:
            # Omit comment lines with #
            line = line.strip().split('#')[0]
            if not line: continue
            if line.startswith('#'): continue

            words = line.split()

            if words[0] == 'rpc':
                MustLen(4, words)
                MustBe('=', words[2])
                # if current: current.finish(self)
                current = RPC(words[1], int(words[3]))
                rpcs[words[1]] = current
    
            elif words[0] == 'in':
                MustLen(4, words)
                MustBe(':', words[2])
                current.ins.append((words[1], words[3]))

            elif words[0] == 'out':
                MustLen(4, words)
                MustBe(':', words[2])
                current.outs.append((words[1], words[3]))

            elif words[0] == 'err':
                MustLen(4, words)
                MustBe('=', words[2])
                current.errs.append((words[1], words[3]))

            #############################################def finish(self):

        for name, rpc in rpcs.items():

            cc(f'// rpc {name} ...')
            cc(f'struct rpc_{name} {{')
            for v, t in rpc.ins:
                cc(f'  T_{t} in_{v};')
            for v, t in rpc.outs:
                cc(f'  T_{t} out_{v};')
            cc(f'}};')
            for e, n in rpc.errs:
                cc(f'constexpr byte Err_{name}_{e} = {n};')
            cc(f'//------------------------------')

        for name, rpc in rpcs.items():
            cc(f'byte Call{name}(struct rpc_{name}* p) {{')
            # Send the RPC number
            cc(f'  putbyte({rpc.num});')
            for v, t in rpc.ins:
                cc(f'  SendIn_{t}(p->in_{v});')
            # expect RPC number back, if OK.
            cc(f'''  byte _err_ = getbyte();
                   if ( _err_ != {rpc.num} ) {{
                       return (_err_ == 0) ? 128 : _err_;
                   }} else {{
                   ''')
            for v, t in rpc.outs:
                cc(f'  RecvOut_{t}(&p->out_{v});')
            cc(f'  return 0; }}')
            cc(f'}}')

        go('package generated')
        go('')
        go('// Generated Go Code')
        go('')
        for name, rpc in rpcs.items():
            go(f'type Rpc_{name} struct {{')
            for v, t in rpc.ins:
                go(f'  In_{v} T_{t}')
            for v, t in rpc.outs:
                go(f'  Out_{v} T_{t}')
            go(f'}}')
            for e, n in rpc.errs:
                go(f'const Err_{name}_{e} = {n}')
            go('')
        go('''
type RpcServer struct {
    Chan chan byte
}
func NewRpcServer() {
    return &RpcServer{
        Chan: make(chan byte),
    }
}
func (o *RpcServer) GetByte() byte {
    a := <-o.Chan
    if a == 1 { // escaped by 1: adds 32 to data, which follows.
        b := <-o.Chan
        return b - 32
    }
    return a
}
func (o *RpcServer) Loop() {
    var buf bytes.Buffer
    for {
        a := o.GetByte()
        switch {
        case a == 13 : {}  // CR
        case a == 10 : {   // LF
            log.Println(buf.String())
            buf.Reset()
        }
        case 32 <= a && a <= 127 : {
            buf.WriteByte(a)   
        }
        case 129 <= a && a <= 254 : { // RPC
            o.CallRpc(a)
        }
        default:
            log.Panicf("RpcServer.Loop: unexpected byte $%02x", a)
        }
    }
}

func (o *RpcServer) CallRpc(a byte) {
    switch a {
''')
        for name, rpc in rpcs.items():
            go(f'''
        case {rpc.num}: {{
            r := &Rpc{{}}
''')
            for n, t in rpc.ins:
                go(f' // IN: {n} {t}')
                try:
                    t = int(t)
                except:
                    pass
                if type(t) == int:
                    go(f'  {{for i:=0; i<{t}; i++ {{')
                    go(f'    r.In_{n}[i] = o.GetByte()')
                    go(f'  }}}}')
                elif t == 'byte':
                    go(f'r.In_{n} = o.GetByte()')
                elif t == 'word':
                    go(f'{{')
                    go(f'   hi := o.GetByte()')
                    go(f'   lo := o.GetByte()')
                    go(f'   r.In_{n} = (uint(hi)<<8) | uint(lo)')
                    go(f'}}')
                else:
                    raise Exception("bad type: %s", t)

            go(f''' // CALL RPC
            HandleRpc{name}(o, r)
''')
            for n, t in rpc.outs:
                go(f' // OUT: {n} {t}')
                try:
                    t = int(t)
                except:
                    pass
                if type(t) == int:
                    go(f'  {{for i:=0; i<{t}; i++ {{')
                    go(f'    o.PutByte(r.In_{n}[i])')
                    go(f'  }}}}')
                elif t == 'byte':
                    go(f'    o.PutByte(r.In_{n})')
                elif t == 'word':
                    go(f'{{')
                    go(f'   o.PetByte( byte(r.In_{n}>>8))')
                    go(f'   o.PetByte( byte(r.In_{n}>>0))')
                    go(f'}}')
                else:
                    raise Exception("bad type: %s", t)


            go(f'''
        }}
''')
        go('''
        default:
            log.Panicf("Unknown RPC number: %d.", rpc.num)
    }
}
''')

if __name__ == '__main__':
    if len(sys.argv) != 4:
        raise Exception('Usage:  python3 rpc-compiler.py definitions hfile gofile')

    Process(*sys.argv[1:])


#def Process(definitions, hfile, gofile):
