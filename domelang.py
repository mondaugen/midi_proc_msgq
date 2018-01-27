# bunk dank dome

import re

# Mode when waiting for operation
# for example, if we are in this mode and we encounter =, we expect a character
# or a string. If either of those is encountered, we start a function definition
DOME_MODE_DISPATCH=1
DOME_MODE_FUNDEF=2
DOME_MODE_INTERPRET=3


def _plus(s,a):
    x=s.pop()
    y=s.pop()
    s.append(x+y)

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
        Parse the cmnds, effecting them on the stack.
        """
        while cmds:
            for n,p,a,f in self.cmd_parsers:
                m=p.match(cmds)
                if m:
                    print(n)
                    aux=None
                    if a:
                        aux=a(m.group(0))
                    if f:
                        f(stack,aux)
                    cmds=cmds[m.span(0)[1]:]
                    continue

d=Dome()
c='123 456.789 + p'
s=[]
d.parse(s,c)
print(s)
