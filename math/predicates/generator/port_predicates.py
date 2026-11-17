#!/usr/bin/env python3
#  Usage: see README.md in this directory.
"""Mechanical port of Shewchuk's public-domain predicates.c to C++ for litestl.

Macros -> inline functions with identical call syntax (inputs by value,
outputs by reference); selected functions copied verbatim with REAL->double;
stage counters inserted at returns; insphereexact's scratch arrays moved to
the heap.  Output files are written to argv[2] (the litestl math/ dir)."""
import re, sys, os

src = open(sys.argv[1]).read()
outdir = sys.argv[2]

# ---------------- macros -> inline functions ----------------
TEMPS = ['bvirt','avirt','bround','around','c','abig','ahi','alo','bhi','blo',
         'err1','err2','err3','_i','_j','_k','_l','_m','_n',
         '_0','_1','_2','_3','_4','_5','_6','a0hi','a0lo','a1hi','a1lo']
macro_re = re.compile(r'^#define (\w+)\(([^)]*)\)\s*((?:.*\\\n)*.*)$', re.M)
macros = []   # (name, params, body_statements)
out_pos = {}  # name -> set of output param indices
for m in macro_re.finditer(src):
    name, params, body = m.group(1), m.group(2), m.group(3)
    if name in ('Absolute',):
        continue
    params = [p.strip() for p in params.replace('\\\n', ' ').split(',')]
    body = body.replace('\\\n', ' ')
    stmts = [s.strip() for s in body.split(';') if s.strip()]
    outs = set()
    for s in stmts:
        am = re.match(r'^(\w+)\s*=', s)
        if am and am.group(1) in params:
            outs.add(params.index(am.group(1)))
        cm = re.match(r'^(\w+)\((.*)\)$', s)
        if cm and cm.group(1) in out_pos:
            args = [a.strip() for a in cm.group(2).split(',')]
            for idx in out_pos[cm.group(1)]:
                if args[idx] in params:
                    outs.add(params.index(args[idx]))
    out_pos[name] = outs
    macros.append((name, params, stmts))

lines = []
for name, params, stmts in macros:
    sig = ', '.join(('double &' if i in out_pos[name] else 'double ') + p
                    for i, p in enumerate(params))
    used = set(re.findall(r'\b\w+\b', ' '.join(stmts)))
    temps = [t for t in TEMPS if t in used and t not in params]
    lines.append(f'inline void {name}({sig})')
    lines.append('{')
    if temps:
        lines.append('  double ' + ', '.join(temps) + ';')
    for s in stmts:
        s = s.replace('(REAL)', '')
        lines.append(f'  {s};')
    lines.append('}')
    lines.append('')
macro_cpp = '\n'.join(lines)

# ---------------- functions ----------------
def func(name):
    m = re.search(r'^(REAL|int) ' + name + r'\(([^)]*)\)[^\n]*\n\{(.*?)\n\}\n', src, re.S | re.M)
    assert m, name
    return m.group(1), m.group(2), m.group(3)

TEMP_SET = set(TEMPS)
def clean_body(body):
    lines = body.split('\n')
    decl_idx = {}
    for i, ln in enumerate(lines):
        dm = re.match(r'^\s*(INEXACT\s+)?REAL\s+([^;\[]*);\s*$', ln)
        if dm:
            names = [n.strip() for n in dm.group(2).split(',')]
            if names and all(n in TEMP_SET for n in names):
                decl_idx[i] = names
    rest = '\n'.join(l for i, l in enumerate(lines) if i not in decl_idx)
    out = []
    for i, ln in enumerate(lines):
        if i in decl_idx:
            keep = [n for n in decl_idx[i] if re.search(r'\b' + re.escape(n) + r'\b', rest)]
            if not keep:
                continue  # macro temporaries now internal to the inline functions
            ln = '  double ' + ', '.join(keep) + ';'
        ln = re.sub(r'\bINEXACT\s+', '', ln).replace('(REAL)', '(double)')
        ln = re.sub(r'\bREAL\b', 'double', ln)
        out.append(ln)
    return '\n'.join(out)

def port(name, rename=None, stages=None, pred=None):
    rt, args, body = func(name)
    rt = 'double' if rt == 'REAL' else rt
    args = re.sub(r'\bREAL\b', 'double', args)
    body = clean_body(body)
    if stages:
        k = [0]
        def rep(m):
            st = stages[min(k[0], len(stages) - 1)]
            k[0] += 1
            return f'PRED_HIT({pred}, {st});\n  {m.group(0)}'
        body = re.sub(r'return\b', rep, body)
        # re-indent inserted counters to the return's indentation
        body = re.sub(r'\n(\s*)PRED_HIT\((.*?)\);\n  return', r'\n\1PRED_HIT(\2);\n\1return', body)
        body = re.sub(r'(\s+)PRED_HIT\(([^)]*)\);\n\s*return', lambda m: f'{m.group(1)}PRED_HIT({m.group(2)});{m.group(1)}return', body)
    return f'{rt} {rename or name}({args})\n{{{body}\n}}\n'

def wrapper_stages(name, pred):
    # every return in the public wrapper except the tail call is stage A
    rt, args, body = func(name)
    n = body.count('return')
    return ['A'] * (n - 1) + ['ADAPT']

def heapify_insphereexact(code):
    decls = re.findall(r'^\s*double\s+([^;]*\[[^;]*);\s*$', code, re.M)
    arrays = []
    for d in decls:
        for item in d.split(','):
            mm = re.match(r'\s*(\w+)\[(\d+)\]\s*$', item)
            assert mm, item
            arrays.append((mm.group(1), int(mm.group(2))))
    code = re.sub(r'^\s*double\s+[^;]*\[[^;]*;\s*\n', '', code, flags=re.M)
    total = sum(n for _, n in arrays)
    setup = [f'  /* {total * 8} bytes of scratch: kept off the stack (WASM / secondary threads). */',
             f'  double *scratch_ = static_cast<double *>(alloc::alloc("insphereexact scratch", {total} * sizeof(double)));']
    off = 0
    for nm, n in arrays:
        setup.append(f'  double *{nm} = scratch_ + {off};')
        off += n
    code = code.replace('{\n', '{\n' + '\n'.join(setup) + '\n', 1)
    code, n = re.subn(r'return\s*\(?deter\[deterlen - 1\]\)?;',
                  'const double result_ = deter[deterlen - 1];\n  alloc::release(scratch_);\n  return result_;', code)
    assert n == 1 and code.count('return') == 1, 'insphereexact must have exactly one return'
    return code, total

HEADER = '''/* Ported from Jonathan Richard Shewchuk's predicates.c (public domain),
 * "Adaptive Precision Floating-Point Arithmetic and Fast Robust Geometric
 * Predicates", Discrete & Computational Geometry 18:305-363, 1997.
 * Generated by a mechanical port (macros -> inline functions in
 * expansion.h); keep edits minimal so the code stays comparable with the
 * original. Compiled with strict IEEE flags (see math/CMakeLists.txt). */
'''
PRE = HEADER + '#include "expansion.h"\n\nnamespace litestl::math::pred::detail {\n\n'
POST = '\n} // namespace litestl::math::pred::detail\n'

def write(fn, text):
    open(os.path.join(outdir, fn), 'w').write(text)

# expansion.h
exp_h = HEADER + '''#pragma once

/* Internal to the math/predicates sources: include only from translation units
 * compiled with the predicate FP flags. */

#include "math/predicates.h"
#include "util/alloc.h"

#include <cfloat>

#if defined(__FAST_MATH__)
#error "math/predicates must not be compiled with -ffast-math"
#endif
static_assert(FLT_EVAL_METHOD == 0, "math/predicates requires FLT_EVAL_METHOD == 0 (no x87)");

#if defined(__clang__)
#pragma clang fp contract(off)
#endif
#pragma STDC FP_CONTRACT OFF

namespace litestl::math::pred::detail {

/* IEEE double, round-to-nearest-even (what exactinit() computes). */
constexpr double epsilon = 0x1.0p-53;
constexpr double splitter = 0x1.0p27 + 1.0;
constexpr double resulterrbound = (3.0 + 8.0 * epsilon) * epsilon;
constexpr double ccwerrboundA = (3.0 + 16.0 * epsilon) * epsilon;
constexpr double ccwerrboundB = (2.0 + 12.0 * epsilon) * epsilon;
constexpr double ccwerrboundC = (9.0 + 64.0 * epsilon) * epsilon * epsilon;
constexpr double o3derrboundA = (7.0 + 56.0 * epsilon) * epsilon;
constexpr double o3derrboundB = (3.0 + 28.0 * epsilon) * epsilon;
constexpr double o3derrboundC = (26.0 + 288.0 * epsilon) * epsilon * epsilon;
constexpr double iccerrboundA = (10.0 + 96.0 * epsilon) * epsilon;
constexpr double iccerrboundB = (4.0 + 48.0 * epsilon) * epsilon;
constexpr double iccerrboundC = (44.0 + 576.0 * epsilon) * epsilon * epsilon;
constexpr double isperrboundA = (16.0 + 224.0 * epsilon) * epsilon;
constexpr double isperrboundB = (5.0 + 72.0 * epsilon) * epsilon;
constexpr double isperrboundC = (71.0 + 1408.0 * epsilon) * epsilon * epsilon;

inline double Absolute(double a)
{
  return a >= 0.0 ? a : -a;
}

''' + macro_cpp + '''
double estimate(int elen, const double *e);
int fast_expansion_sum_zeroelim(int elen, const double *e, int flen, const double *f, double *h);
int scale_expansion_zeroelim(int elen, const double *e, double b, double *h);

/* Stage counters (compiled in only for the predicates_stats library). */
#ifdef LITESTL_PRED_STATS
void stat_hit(Predicate p, Stage s);
#define PRED_HIT(p, s) ::litestl::math::pred::detail::stat_hit(Predicate::p, Stage::s)
#else
#define PRED_HIT(p, s) ((void)0)
#endif

double orient2d(const double *pa, const double *pb, const double *pc);
double orient3d(const double *pa, const double *pb, const double *pc, const double *pd);
double incircle(const double *pa, const double *pb, const double *pc, const double *pd);
double insphere(const double *pa, const double *pb, const double *pc, const double *pd, const double *pe);

} // namespace litestl::math::pred::detail
'''
write('predicates/expansion.h', exp_h)

def fix_const(code):
    # helper signatures take const pointers
    code = code.replace('estimate(int elen, double * e)', 'estimate(int elen, const double *e)')
    code = code.replace('fast_expansion_sum_zeroelim(int elen, double * e, int flen, double * f, double * h)',
                        'fast_expansion_sum_zeroelim(int elen, const double *e, int flen, const double *f, double *h)')
    code = code.replace('scale_expansion_zeroelim(int elen, double * e, double b, double * h)',
                        'scale_expansion_zeroelim(int elen, const double *e, double b, double *h)')
    return code

ADAPT = ['B', 'T', 'C', 'D']
exp_cc = PRE + fix_const(port('estimate') + '\n' + port('fast_expansion_sum_zeroelim') + '\n' +
                         port('scale_expansion_zeroelim')) + POST
orient_cc = PRE + 'namespace {\n' + port('orient2dadapt', stages=ADAPT, pred='Orient2d') + '\n' + \
    port('orient3dadapt', stages=ADAPT, pred='Orient3d') + '} // namespace\n\n' + \
    port('orient2d', stages=wrapper_stages('orient2d', 'Orient2d'), pred='Orient2d') + '\n' + \
    port('orient3d', stages=wrapper_stages('orient3d', 'Orient3d'), pred='Orient3d') + POST
incircle_cc = PRE + 'namespace {\n' + port('incircleadapt', stages=ADAPT, pred='Incircle') + '} // namespace\n\n' + \
    port('incircle', stages=wrapper_stages('incircle', 'Incircle'), pred='Incircle') + POST
ie, total = heapify_insphereexact(port('insphereexact'))
insphere_cc = PRE + 'namespace {\n' + ie + '\n' + port('insphereadapt', stages=['B', 'T', 'C', 'D'], pred='Insphere') + \
    '} // namespace\n\n' + port('insphere', stages=wrapper_stages('insphere', 'Insphere'), pred='Insphere') + POST
# ADAPT pseudo-stage marks the hand-off: no counter
for name, text in [('expansion.cc', exp_cc), ('orient.cc', orient_cc), ('incircle.cc', incircle_cc), ('insphere.cc', insphere_cc)]:
    text = re.sub(r'\n\s*PRED_HIT\(\w+, ADAPT\);', '', text)
    text = text.replace('alloc::', '::litestl::alloc::')
    write('predicates/' + name, text)
print('insphereexact scratch doubles:', total)
