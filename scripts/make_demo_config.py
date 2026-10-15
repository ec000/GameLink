#!/usr/bin/env python3
"""Generate local mock configuration and private test credentials. Not VPS configuration."""
import argparse, hashlib, json, os, pathlib, secrets
p=argparse.ArgumentParser();p.add_argument('--directory',default='build/demo');args=p.parse_args()
out=pathlib.Path(args.directory).resolve();out.mkdir(parents=True,exist_ok=True)
config_path=out/'server.json';credential_path=out/'credentials.json'
if config_path.exists() or credential_path.exists():
    raise SystemExit('Refusing to overwrite existing demo credentials/configuration.')
users=[];tokens={}
for name,node in [('alice','aaaaaaaaaa'),('bob','bbbbbbbbbb')]:
    token=secrets.token_hex(32);tokens[name]=token
    users.append(dict(id=name,nodeId=node,tokenHash=hashlib.sha256(token.encode()).hexdigest()))
config=dict(listen='127.0.0.1:8080',controllerUrl='http://127.0.0.1:9993',controllerTokenFile='',
            stateFile=str(out/'state.mock.json'),networks=['abcdef1234000001','abcdef1234000002'],users=users,ttlSeconds=3600)
for path,data in [(config_path,config),(credential_path,tokens)]:
    fd=os.open(path,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
    with os.fdopen(fd,'w') as f:json.dump(data,f,indent=2)
print(f'Mock config: {config_path}\nPrivate credentials: {credential_path}\nNo real ZeroTier authorization.')
