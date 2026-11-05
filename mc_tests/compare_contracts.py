#!/usr/bin/env python3
"""Small, reproducible comparison, not a claim of exclusive tool coverage."""
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
headers = '#define _GNU_SOURCE\n#include <stdlib.h>\n#include <pthread.h>\n#include <sys/mman.h>\n'
cases = {
    'pthread_negative': 'int f(pthread_mutex_t*m){int r=pthread_mutex_lock(m);if(r<0)return -1;return 0;}',
    'pthread_minus_one': 'int f(pthread_t t){int r=pthread_join(t,0);if(r==-1)return -1;return 0;}',
    'mmap_null': 'void*f(void){void*p=mmap(0,4096,PROT_READ,MAP_PRIVATE,-1,0);if(p==0)return p;return p;}',
    'strtol_minus_one': 'long f(char*s){long n=strtol(s,0,10);if(n==-1)return 0;return n;}',
    'pthread_correct': 'int f(pthread_mutex_t*m){int r=pthread_mutex_lock(m);if(r!=0)return -1;return 0;}',
    'mmap_correct': 'void*f(void){void*p=mmap(0,4096,PROT_READ,MAP_PRIVATE,-1,0);if(p==MAP_FAILED)return 0;return p;}',
}
commands = {
    'mancheck': [str(root/'analyzer/analyzer'),'--json','--no-db'],
    'clang --analyze': ['clang','--analyze','-Xanalyzer','-analyzer-output=text','-std=c11'],
    'cppcheck': ['cppcheck','--enable=warning','--std=c11','--suppress=missingIncludeSystem','--quiet'],
    'gcc -fanalyzer': ['gcc','-fanalyzer','-c','-o','/dev/null','-std=c11'],
}
report = {'scope':'Only these six valid C examples and the recorded commands; no exclusivity claim.', 'tools':{}, 'cases':{}}
for name, command in commands.items():
    if name != 'mancheck' and not shutil.which(command[0]): continue
    version = subprocess.run([command[0],'--version'],capture_output=True,text=True).stdout.splitlines()
    report['tools'][name] = {'command':command, 'version':version[0] if version else 'workspace build'}
with tempfile.TemporaryDirectory(prefix='mancheck-comparison-') as directory:
    for name, code in cases.items():
        src = Path(directory)/(name+'.c'); src.write_text(headers+code)
        valid = subprocess.run(['clang','-std=c11','-fsyntax-only','-Werror=implicit-function-declaration',str(src)],capture_output=True,text=True)
        if valid.returncode: raise RuntimeError(valid.stderr)
        row = {}
        for tool in report['tools']:
            result = subprocess.run([*commands[tool],str(src)],capture_output=True,text=True,timeout=30)
            if result.returncode: raise RuntimeError(f'{tool} failed: {result.stderr}')
            if tool == 'mancheck':
                diagnostics = json.loads(result.stdout)['files'][0]['diagnostics']
                row[tool] = [d['category'] for d in diagnostics]
            else: row[tool] = result.stderr.strip()
        report['cases'][name] = {'source':headers+code, 'results':row}
out = root/'mc_tests/out/contract_comparison.json'; out.parent.mkdir(exist_ok=True)
out.write_text(json.dumps(report,indent=2)+'\n')
for name, item in report['cases'].items():
    print(name+': '+', '.join(tool+'='+('diagnostic' if result else 'quiet') for tool,result in item['results'].items()))
print(out)
