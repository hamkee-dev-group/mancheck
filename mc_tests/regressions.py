#!/usr/bin/env python3
"""Deterministic regressions: valid C, explicit positive/negative expectations.
No dependency on installed manpages. Set MANCHECK_ANALYZER / MANCHECK_BUILDER
for sanitizer builds. All generated files live in a temporary directory.
"""
import json
import os
from pathlib import Path
import sqlite3
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ANALYZER = Path(os.environ.get('MANCHECK_ANALYZER', ROOT / 'analyzer/analyzer')).resolve()
BUILDER = Path(os.environ.get('MANCHECK_BUILDER', ROOT / 'specdb/specdb-build')).resolve()
ENV = dict(os.environ)
ENV.pop('MANCHECK_AUDIT_ENV', None)
HEADERS = '''#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <err.h>
#include <syslog.h>
'''
count = 0


def run(args, **kw):
    result = subprocess.run([str(x) for x in args], text=True, capture_output=True, env=kw.pop('env', ENV), timeout=30, **kw)
    for marker in ('ERROR: AddressSanitizer', 'ERROR: LeakSanitizer', 'runtime error:', 'AddressSanitizer:DEADLYSIGNAL'):
        assert marker not in result.stderr, result.stderr
    return result


def check(ok, message):
    global count
    assert ok, message
    count += 1


with tempfile.TemporaryDirectory(prefix='mancheck-regressions-') as directory:
    tmp = Path(directory)

    def analyze(name, code, expected, *, options=(), compile=True, env=ENV):
        src = tmp / (name + '.c')
        src.write_text(HEADERS + code)
        if compile:
            cc = run(['clang', '-std=c11', '-fsyntax-only', '-Werror=implicit-function-declaration', src])
            check(cc.returncode == 0, name + ': invalid fixture: ' + cc.stderr)
        p = run([ANALYZER, '--no-db', '--json', *options, src], env=env)
        check(p.returncode == 0, name + ': analyzer failed: ' + p.stderr)
        doc = json.loads(p.stdout)['files'][0]
        actual = sorted(d['category'] for d in doc['diagnostics'])
        check(actual == sorted(expected), f'{name}: expected {expected}, got {doc}')
        check(doc['issue_count'] == len(actual), name + ': count mismatch')
        return src, doc

    analyze('assigned_condition', 'void f(int fd,char *b){ssize_t n; if((n=read(fd,b,1))<0)return;}', [])
    analyze('nested_check', 'void f(int fd,char*b){ssize_t n=read(fd,b,1); {if(n<0)return;}}', [])
    analyze('overwritten', 'void f(int fd,char*b){ssize_t n=read(fd,b,1); n=0; if(n<0)return;}', ['return_value_check'])
    analyze('partial_check', 'void f(int fd,char*b,int c){ssize_t n=read(fd,b,1); if(c){if(n<0)return;}}', ['return_value_check'])
    analyze('string_not_check', 'void f(int fd,char*b){ssize_t n=read(fd,b,1); if(strcmp("n","x"))return;}', ['return_value_check'])
    analyze('shadow_not_check', 'void f(int fd,char*b){ssize_t n=read(fd,b,1); {int n=0;if(n)return;}}', ['return_value_check'])
    analyze('alias_check', 'void f(int fd,char*b){ssize_t n=read(fd,b,1); ssize_t m=n; if(m<0)return;}', [])
    analyze('comma_discard', 'int f(int fd,char*b){return (read(fd,b,1),0);}', ['return_value_check'])
    analyze('ternary_discard', 'void f(int fd,char*b,int c){c?read(fd,b,1):0;}', ['return_value_check'])
    analyze('parenthesized_call', 'void f(int fd,char*b){(read)(fd,b,1);}', ['return_value_check'])
    analyze('comma_condition', 'void f(int fd,char*b){if((read(fd,b,1),1))return;}', ['return_value_check'])
    analyze('double_free', 'void f(void*p){free(p);free(p);return;}', ['double_close'])
    analyze('exclusive_free', 'void f(void*p,int c){if(c)free(p);else free(p);}', [])
    analyze('shadow_free', 'void f(void*p,void*q){free(p);{void*p=q;free(p);}}', [])
    analyze('null_free', 'void f(void*p){free(p);p=NULL;free(p);free(NULL);free(NULL);}', [])
    analyze('alias_free', 'void f(void*p){void*q=p;free(p);free(q);}', ['double_close'])
    analyze('loop_free', 'void f(void*p){for(int i=0;i<2;i++){free(p);free(p);}}', ['double_close'])
    analyze('one_iteration', 'void f(void*p){for(int i=0;i<1;i++)free(p);}', [])
    analyze('fresh_loop', 'void f(int n){for(int i=0;i<n;i++){void*p=malloc(4);if(!p)return;free(p);}}', [])
    analyze('goto_cleanup', 'void f(int c){void*p=malloc(4);if(!p)return;if(c)goto done; done:free(p);}', [])
    analyze('goto_double', 'void f(void*p){free(p);goto done;done:free(p);}', ['double_close'])
    analyze('leak', 'void f(int c){void*p=malloc(4);if(!p)return;if(c)return;free(p);}', ['missing_cleanup'])
    analyze('cleanup', 'void f(void){void*p=malloc(4);if(!p)return;free(p);}', [])
    analyze('returned_ownership', 'void *f(void){void*p=malloc(4);if(!p)return NULL;return p;}', [])
    analyze('escaped_ownership', 'extern void take(void*);void f(void){void*p=malloc(4);if(!p)return;take(p);}', [])
    analyze('pipe_wrong_release', 'void f(void){FILE*p=popen("true","r");if(!p)return;if(fclose(p))return;}', ['resource_mismatch'])
    analyze('pipe_right_release', 'void f(void){FILE*p=popen("true","r");if(!p)return;if(pclose(p))return;}', [])
    analyze('pthread_negative', 'void f(pthread_mutex_t*m){int r=pthread_mutex_lock(m);if(r<0)return;}', ['wrong_error_check'])
    analyze('pthread_zero', 'void f(pthread_mutex_t*m){int r=pthread_mutex_lock(m);if(r!=0)return;}', [])
    analyze('mmap_null', 'void*f(void){void*p=mmap(NULL,4096,PROT_READ,MAP_PRIVATE,-1,0);if(p==NULL)return p;return p;}', ['wrong_error_check'])
    analyze('mmap_failed', 'void*f(void){void*p=mmap(NULL,4096,PROT_READ,MAP_PRIVATE,-1,0);if(p==MAP_FAILED)return NULL;return p;}', [])
    analyze('strtol_sentinel', 'long f(char*s){long n=strtol(s,NULL,10);if(n==-1)return 0;return n;}', ['wrong_error_check'])
    analyze('format_literals', 'void f(void){printf("%" "d",1);printf(("%d"),1);warn("%d",42);}', [])
    analyze('format_arg', 'void f(char*s){warn(s,"literal");}', ['format_string'])
    analyze('infallible', 'void f(char*b){memset(b,0,1);free(NULL);srand(1);closelog();openlog("x",0,0);}', [])
    analyze('environment_default', 'void f(void){getenv("PATH");}', [])
    analyze('malloc_whitespace', 'void f(void){int*p=malloc(sizeof ( p ));if(!p)return;free(p);}', ['malloc_size_mismatch'])
    analyze('calloc_type', 'void f(void){int*p=calloc(4,sizeof(int));if(!p)return;free(p);}', [])
    analyze('malloc_pointee', 'void f(void){int*p=malloc(sizeof ( *p ));if(!p)return;free(p);}', [])
    analyze('wide_block', 'void f(void*p){free(p);free(p);' + '(void)0;' * 600 + '}', ['double_close'])
    analyze('line_directive', 'void before(void){}\n#line 100 "generated.c"\nvoid f(int fd,char*b){read(fd,b,1);}', ['return_value_check'])
    analyze('switch_exclusive', 'void f(void*p,int c){switch(c){case 1:free(p);break;default:free(p);break;}}', [])
    analyze('switch_default_checks', 'void f(int fd,char*b,int c){ssize_t n=read(fd,b,1);switch(c){default:if(n<0)return;}}', [])
    analyze('switch_check', 'void f(int fd,char*b){ssize_t n=read(fd,b,1);switch(n){case -1:return;default:return;}}', [])

    # All output modes consume the same ordered findings and persist them once.
    src, doc = analyze('ordered', 'void f(int fd,char*b){\n// mc:ignore-next-warning\nint*p=malloc(sizeof(p));\nread(fd,b,1);\nfree(p);\n}', ['return_value_check', 'return_value_check'])
    text = run([ANALYZER, '--no-db', src])
    check(len(text.stdout.splitlines()) == doc['issue_count'], 'text/JSON parity')
    sarif = run([ANALYZER, '--no-db', '--sarif', src])
    check(len(json.loads(sarif.stdout)['runs'][0]['results']) == doc['issue_count'], 'SARIF/JSON parity')
    db = tmp / 'findings.db'
    logged = run([ANALYZER, '--db', db, '--json', src])
    with sqlite3.connect(db) as conn:
        check(conn.execute('select count(*) from facts').fetchone()[0] == doc['issue_count'], 'JSON facts missing')
    sup = tmp / 'suppressions'
    sup.write_text('* return_value_check\n')
    analyze('literal_snprintf_suppressed', 'void f(void){char b[4];snprintf(b,sizeof b,"x");}', [], options=['--suppressions', sup])

    # Source validity and preprocessing errors are failures, not clean runs.
    missing = run([ANALYZER, '--no-db', tmp / 'missing.c'])
    check(missing.returncode != 0 and 'cannot read' in missing.stderr, 'missing file silently succeeds')
    bad = tmp / 'bad.c'
    bad.write_text('#include "missing.h"\nvoid f(void){}')
    check(run([ANALYZER, '--no-db', bad]).returncode != 0, 'preprocessor failure silently succeeds')
    bad.write_text('void f( {')
    broken = run([ANALYZER, '--json', '--no-db', bad])
    check(broken.returncode != 0 and not json.loads(broken.stdout)['files'][0]['complete'], 'parse errors silently succeed')
    typo = tmp / 'typo.db'
    check(run([ANALYZER, '--specdb', typo, '--no-db', src]).returncode != 0 and not typo.exists(), 'specdb typo creates database')

    # compile_commands arguments are not reinterpreted as shell source.
    compdb = tmp / 'compile_commands.json'
    forced = tmp / 'forced.h'; forced.write_text('#define ENABLE_TEST 1\n')
    source = tmp / 'flags.c'
    source.write_text('#include <unistd.h>\nvoid f(int fd,char*b){\n#if ENABLE_TEST && _REENTRANT\nread(fd,b,1);\n#endif\n}')
    compdb.write_text(json.dumps([{'directory':str(tmp), 'file':source.name, 'arguments':['cc','-include',str(forced),'-pthread','-c',source.name]}]))
    output = run([ANALYZER, '--json', '--no-db', '--compile-commands', compdb, source])
    check(output.returncode == 0 and json.loads(output.stdout)['files'][0]['issue_count'] == 1, 'forced include/thread flags lost')
    unicode_src = tmp / 'café.c'; unicode_src.write_text('void f(void){}')
    compdb.write_text(json.dumps([{'directory':str(tmp), 'file':unicode_src.name, 'arguments':['cc','-c',unicode_src.name]}], ensure_ascii=True))
    check(run([ANALYZER,'--no-db','--compile-commands',compdb,unicode_src]).returncode == 0, 'Unicode compilation database rejected')
    compdb.write_text('[{"file":"x.c","file":123}]')
    p = run([ANALYZER,'--no-db','--compile-commands',compdb,source])
    check(p.returncode > 0 and 'AddressSanitizer' not in p.stderr, 'malformed compilation database crashed')

    for malformed in ('[{"file":"x.c","unknown":tru}]',
                      '[{"file":"x.c","unknown":[1}}]',
                      '[{"file":"x.c","unknown":01}]',
                      '[{"file":"x.c","unknown":{"a":}}]',
                      '[{"file":"x.c"}] trailing'):
        compdb.write_text(malformed)
        check(run([ANALYZER,'--no-db','--compile-commands',compdb,source]).returncode > 0, 'invalid JSON accepted: '+malformed)
    source.write_text("#include <unistd.h>\nvoid f(int fd,char*b){\n#if ESCAPED_CHAR == '\\n'\nread(fd,b,1);\n#endif\n}")
    arguments = ['cc', "-DESCAPED_CHAR='\\n'", '-c', source.name]
    for form in ('arguments', 'command'):
        entry = {'directory':str(tmp), 'file':source.name, form:arguments if form == 'arguments' else shlex.join(arguments)}
        compdb.write_text(json.dumps([entry]))
        output = run([ANALYZER,'--json','--no-db','--compile-commands',compdb,source])
        check(output.returncode == 0 and json.loads(output.stdout)['files'][0]['issue_count'] == 1, form+': backslash quoting corrupted')
    for argument in ('@response.txt', '-Wp,-DTEST=1'):
        compdb.write_text(json.dumps([{'directory':str(tmp), 'file':source.name, 'arguments':['cc',argument,'-c',source.name]}]))
        check(run([ANALYZER,'--no-db','--compile-commands',compdb,source]).returncode > 0, 'unsupported build flags silently ignored')
    compdb.write_text(json.dumps([{'directory':str(tmp), 'file':source.name, 'command':"cc -DTEST='unterminated"}]))
    check(run([ANALYZER,'--no-db','--compile-commands',compdb,source]).returncode > 0, 'unclosed command quote accepted')
    # Verify minimal preprocessing preserves literal spelling, including URLs.
    source.write_text('const char *s="https://example.test/a  b"; /* comment */\n')
    views = tmp/'views.jsonl'
    check(run([ANALYZER,'--no-db','--dump-views',views,source]).returncode == 0, 'view generation failed')
    view = json.loads(views.read_text())
    check('"https://example.test/a  b"' in view['min'] and 'comment' not in view['min'], 'minimal view damaged string literal')
    # Persistence failure is an analysis failure in all output modes.
    with sqlite3.connect(db) as conn:
        conn.execute("create trigger fail_fact before insert on facts begin select raise(ABORT,'test write failure'); end")
    output = run([ANALYZER,'--json','--db',db,src])
    check(output.returncode > 0 and not json.loads(output.stdout)['complete'], 'failed diagnostic persistence reported success')
    with sqlite3.connect(db) as conn:
        statuses = [r[0] for r in conn.execute('select status from runs order by id')]
    check(statuses[0] == 'completed' and statuses[-1] == 'failed', 'run status not recorded: '+str(statuses))

    # Run database schema: legacy/interrupted schemas are completed and
    # versioned; foreign or newer databases are refused untouched.
    legacy = tmp / 'legacy.db'
    with sqlite3.connect(legacy) as conn:
        conn.execute('create table runs (id integer primary key autoincrement, filename text not null, ts datetime default current_timestamp, error_count integer default 0)')
        conn.execute("insert into runs(filename) values ('old.c')")
    output = run([ANALYZER,'--json','--db',legacy,src])
    check(output.returncode == 0 and json.loads(output.stdout)['complete'], 'interrupted legacy schema not completed: '+output.stderr)
    with sqlite3.connect(legacy) as conn:
        check(conn.execute('pragma user_version').fetchone()[0] == 1, 'schema version not recorded')
        check(conn.execute('select count(*) from facts').fetchone()[0] == doc['issue_count'], 'facts missing after schema repair')
        check([r[0] for r in conn.execute('select status from runs order by id')] == ['started', 'completed'], 'legacy run status')
    foreign = tmp / 'foreign.db'
    with sqlite3.connect(foreign) as conn:
        conn.execute('create table runs (id integer primary key, started_at text)')
    output = run([ANALYZER,'--db',foreign,src])
    with sqlite3.connect(foreign) as conn:
        tables = sorted(r[0] for r in conn.execute("select name from sqlite_master where type='table'"))
        version = conn.execute('pragma user_version').fetchone()[0]
    check(output.returncode > 0 and tables == ['runs'] and version == 0, 'foreign database modified: '+str(tables))
    newer = tmp / 'newer.db'
    with sqlite3.connect(newer) as conn:
        conn.execute('pragma user_version=99')
    check(run([ANALYZER,'--db',newer,src]).returncode > 0, 'newer schema version accepted')

    # Project-header function bodies are analyzed and attributed to the
    # header; system headers are not. Main-file suppression markers do not
    # apply to header lines.
    inc = tmp / 'inc'; inc.mkdir()
    (inc / 'proj.h').write_text('#include <unistd.h>\n#include <stdlib.h>\nstatic inline void h(int fd,char*b){read(fd,b,1);}\n')
    main_src = tmp / 'uses_header.c'
    main_src.write_text('// mc:ignore-next-warning\n#include "proj.h"\nvoid g(void){}\n')
    compdb.write_text(json.dumps([{'directory':str(tmp), 'file':main_src.name, 'arguments':['cc','-I'+str(inc),'-c',main_src.name]}]))
    output = run([ANALYZER,'--json','--no-db','--compile-commands',compdb,main_src])
    diags = json.loads(output.stdout)['files'][0]['diagnostics']
    check(output.returncode == 0 and [(d['category'], d['path'], d['line']) for d in diags] == [('return_value_check', str(inc/'proj.h'), 3)],
          'project header body not analyzed or misattributed: '+str(diags))
    text = run([ANALYZER,'--no-db','--compile-commands',compdb,main_src])
    check(text.stdout.startswith(str(inc/'proj.h')+':3:'), 'text output names wrong file: '+text.stdout)
    compdb.write_text(json.dumps([{'directory':str(tmp), 'file':main_src.name, 'arguments':['cc','-isystem',str(inc),'-c',main_src.name]}]))
    main_src.write_text('#include <proj.h>\nvoid g(void){}\n')
    output = run([ANALYZER,'--json','--no-db','--compile-commands',compdb,main_src])
    check(output.returncode == 0 and json.loads(output.stdout)['files'][0]['issue_count'] == 0, 'system header bodies analyzed')

    # Synthetic man provider: exercise parsing, argv safety, exit status, and refresh rollback.
    bin_dir = tmp / 'bin'; bin_dir.mkdir()
    man = bin_dir / 'man'
    page = tmp / 'page'
    page.write_text('''NAME
    widget, log_widget, clear_widget - test API
SYNOPSIS
    #include <widget.h>
    int widget(void);
    void log_widget(int priority, const char *fmt, ...);
    void clear_widget(void);
RETURN VALUE
    widget() returns zero on success. On error, -1 is returned and errno is set.

    clear_widget() returns no value.
DESCRIPTION
    Historical implementations were obsolete. Use clear_widget().
''')
    man.write_text('#!/bin/sh\ncat "$MANCHECK_TEST_PAGE"\n')
    man.chmod(0o755)
    env = dict(ENV, PATH=str(bin_dir)+os.pathsep+ENV['PATH'], MANCHECK_TEST_PAGE=str(page))
    spec = tmp / 'spec.db'
    check(run([BUILDER,spec,'3','widget'],env=env).returncode == 0, 'synthetic import failed')
    with sqlite3.connect(spec) as conn:
        proto = conn.execute('select proto from functions').fetchone()[0]
        check(proto == 'int widget(void);', 'prototype extraction selected include: '+str(proto))
    analyze('custom_contract', 'extern int widget(void);void f(void){widget();}', ['return_value_check'], options=['--specdb',spec])
    analyze('shared_void', 'extern void clear_widget(void);void f(void){clear_widget();}', [], options=['--specdb',spec])
    analyze('shared_format', 'extern void log_widget(int,const char*,...);void f(char*s){log_widget(0,s,"literal");}', ['format_string'], options=['--specdb',spec])
    # Fault halfway through replace_sections must preserve all previously indexed data.
    with sqlite3.connect(spec) as conn:
        before = conn.execute('select section_name,content from function_sections order by id').fetchall()
        conn.execute("create trigger fail_insert before insert on function_sections begin select raise(ABORT,'test insertion failure'); end")
    page.write_text(page.read_text().replace('test API','changed API'))
    check(run([BUILDER,spec,'3','widget'],env=env).returncode != 0, 'failed refresh succeeded')
    with sqlite3.connect(spec) as conn:
        check(conn.execute('select section_name,content from function_sections order by id').fetchall() == before, 'failed refresh destroyed sections')
        conn.execute('drop trigger fail_insert')
    sentinel = tmp / 'injected'
    run([BUILDER,spec,'3; touch '+str(sentinel),'widget'],env=env)
    check(not sentinel.exists(), 'builder executes section as shell code')
    man.write_text('#!/bin/sh\nprintf x\n')
    check(run([BUILDER,spec,'3','x'],env=env).returncode != 0, 'one-byte malformed output accepted')
    man.write_text('#!/bin/sh\ncat "$MANCHECK_TEST_PAGE"\nexit 2\n')
    check(run([BUILDER,spec,'3','widget'],env=env).returncode != 0, 'partial failed man output accepted')

print(f'{count} deterministic regression assertions passed')
