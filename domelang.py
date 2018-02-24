# bunk dank dome

import re

# Mode when waiting for operation
# for example, if we are in this mode and we encounter =, we expect a character
# or a string. If either of those is encountered, we start a function definition
DOME_MODE_DISPATCH=1
DOME_MODE_FUNDEF=2
DOME_MODE_INTERPRET=3

DEBUG=False

def _op_plus(x,y):
    return x+y
def _op_times(x,y):
    return x*y
def _op_divide(x,y):
    return x/y
def _op_subtract(x,y):
    return x-y

def _vop(x,y,op):
    if type(x) == list:
        lx = len(x)
        if type(y) == list:
            ly = len(y)
            for i in range(lx):
                x[i] = _vop(x[i],y[i%ly],op)
        else:
            for i in range(lx):
                x[i] = _vop(x[i],y,op)
        return x
    else:
        if type(y) == list:
            return op(x,y[0])
        else:
            return op(x,y)

def _bin_op(s,a,state=None):
    op=a
    x=s.pop()
    y=s.pop()
    s.append(_vop(y,x,op))
    # TODO: opt can be set to change how _bin_op works

def _list_depth(l,d):
    if type(l) == list:
        return max(map(lambda m: _list_depth(m,d+1),l))
    else:
        return d

def _vindex_get(lhs,rhs):
    """
    Vectorized indexing, get method
    """
    ret=[]
    ld_rhs=_list_depth(rhs,0)
    if ld_rhs == 1:
        for r in rhs:
            if type(lhs) != list:
                ret.append(lhs)
            else:
                ret.append(lhs[r%len(lhs)])
    elif ld_rhs == 0:
        # rhs is not a list
        r = rhs
        if type(lhs) != list:
            ret = lhs
        else:
            ret = lhs[r%len(lhs)]
    else:
        if type(lhs) != list:
            for r in rhs:
                ret.append(_vindex_get(lhs,r))
        else:
            for i,l in enumerate(lhs):
                ret.append(_vindex_get(l,rhs[i%len(rhs)]))
    return ret

def _vindex_get_op(s,a,state=None):
#    op=a
    x=s.pop()
    y=s.pop()
    s.append(_vindex_get(y,x))

def _vappend(x,y):
    x.append(y)
    return x

def _vappend_op(s,a,state=None):
    """
    Appends last item on stack to item before if it is a list,
    if the stack only contains 1 item, make this item into a list
    if the penultimate stack item isn't a list, make it so before appending the
    item
    """
    x=s.pop()
    if (len(s) == 0):
        s.append([x])
        return
    y=s.pop()
    if (type(y) != list):
        y=[y]
    s.append(_vappend(y,x))

def _vpop_op(s,a,state=None):
    """
    Takes the list off the top of the stack and puts it back with 1 less item
    and after that puts the item popped off.
    """
    x=s.pop()
    if (type(x) != list):
        s.append(x)
        return
    s.append(x[:-1])
    s.append(x[-1])

# Note these are stored in the order that they are matched against. The first
# one to match is executed.
cmd_parsers=[
        # Command name, pattern matcher, auxilary preparation function, function
        # command name can be any string
        # pattern matcher is a regex that can be interpreted by the re module to
        # give a matching object
        # auxilary preparation function accepts the string parsed and a state
        # variable and returns data necessary for "function" to evalutate
        # function is passed the stack and the auxilary function and returns its
        # result by operating on the stack
        (
            'FLOAT',
            '[-+]?\d+\.\d*([eE][-+]?\d+|)',
            lambda x,s: float(x),
            lambda s,a,t: s.append(a)
        ),
        (
            'INT',
            '[-+]?\d+',
            lambda x,s: int(x),
            lambda s,a,t: s.append(a)
        ),
        (
            'PLUS',
            '[+]',
            lambda x,s: _op_plus,
            _bin_op
        ),
        (
            'TIMES',
            '×',
            lambda x,s: _op_times,
            _bin_op
        ),
        (
            'DIVIDE',
            '÷',
            lambda x,s: _op_divide,
            _bin_op
        ),
        (
            'SUBTRACT',
            '-',
            lambda x,s: _op_subtract,
            _bin_op
        ),
        (
            'INDEX_GET',
            '\]',
            lambda x,s: _vindex_get,
            _vindex_get_op
        ),
        (
            'APPEND',
            '\(',
            lambda x,s: _vappend,
            _vappend_op
        ),
        (
            'POP',
            '\)',
            lambda x,s: None,
            _vpop_op
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
            lambda s,a,t: print(s[-1])
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
        # Eventually we will use this to pass information to operators
        self.state=None
        
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
                        aux=a(m.group(0),self.state)
                    if (DEBUG):
                        print('command: %s' % (n,))
                        print('stack: ',end='')
                        print(stack)
                    if f:
                        f(stack,aux,self.state)
                    else:
                        if (DEBUG):
                            print("Warning, no function")

                    cmds=cmds[m.end(0):]
                    matched=True
                    break
            if not matched:
                print("Error: no match for %s" % (cmds,))
                break
