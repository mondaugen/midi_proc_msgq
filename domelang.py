# bunk dank dome

import re

# Mode when waiting for operation
# for example, if we are in this mode and we encounter =, we expect a character
# or a string. If either of those is encountered, we start a function definition
DOME_MODE_DISPATCH=1
DOME_MODE_FUNDEF=2
DOME_MODE_INTERPRET=3

DEBUG=False

def _plus(s,a):
    x=s.pop()
    y=s.pop()
    s.append(y+x)

def _times(s,a):
    x=s.pop()
    y=s.pop()
    s.append(y*x)

def _divide(s,a):
    x=s.pop()
    y=s.pop()
    s.append(y/x)

def _subtract(s,a):
    x=s.pop()
    y=s.pop()
    s.append(y-x)

# Note these are stored in the order that they are matched against. The first
# one to match is executed.
cmd_parsers=[
        # Command name, pattern matcher, auxilary preparation function, function
        (
            'FLOAT',
            '[-+]?\d+\.\d*([eE][-+]?\d+|)',
            lambda x: float(x),
            lambda s,a: s.append(a)
        ),
        (
            'INT',
            '[-+]?\d+',
            lambda x: int(x),
            lambda s,a: s.append(a)
        ),
        (
            'PLUS',
            '[+]',
            None,
            _plus
        ),
        (
            'TIMES',
            '×',
            None,
            _times
        ),
        (
            'DIVIDE',
            '÷',
            None,
            _divide
        ),
        (
            'NOP',
            ' ',
            None,
            None
        ),
        (
            'PRINT',
            'p',
            None,
            lambda s,a: print(s[-1])
        )
]


class Dome:
    def __init__(self):
        self.mode = DOME_MODE_DISPATCH
        self.cmd_parsers=[]
        for n,p,a,f in cmd_parsers:
            self.cmd_parsers.append((n,re.compile(p),a,f))
        for t in cmd_parsers:
            print(t)
    def parse(self,stack,cmds):
        """
        Parse the commands, effecting them on the stack.
        """
        while cmds:
            matched=False
            for n,p,a,f in self.cmd_parsers:
                m=p.match(cmds)
                if m:
                    aux=None
                    if a:
                        aux=a(m.group(0))
                    if (DEBUG):
                        print('command: %s' % (n,))
                        print('stack: ',end='')
                        print(stack)
                    if f:
                        f(stack,aux)
                    else:
                        if (DEBUG):
                            print("Warning, no function")

                    cmds=cmds[m.end(0):]
                    matched=True
                    break
            if not matched:
                print("Error: no match for %s" % (cmds,))
                break
