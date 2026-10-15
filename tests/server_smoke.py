#!/usr/bin/env python3
"""Real process / loopback HTTP smoke, no external network or real ZeroTier."""
import argparse,hashlib,json,os,pathlib,secrets,socket,subprocess,tempfile,time,urllib.request,urllib.error
p=argparse.ArgumentParser();p.add_argument('--binary',required=True);args=p.parse_args()
binary=str(pathlib.Path(args.binary).resolve())
with tempfile.TemporaryDirectory(prefix='gamelink-smoke-') as tmp:
    s=socket.socket();s.bind(('127.0.0.1',0));port=s.getsockname()[1];s.close()
    tokens={name:secrets.token_hex(32) for name in ['alice','bob']}
    config=dict(listen=f'127.0.0.1:{port}',controllerUrl='http://127.0.0.1:9993',controllerTokenFile='',
      stateFile=tmp+'/state.json',networks=['abcdef1234000001'],ttlSeconds=30,
      users=[dict(id=n,nodeId=node,tokenHash=hashlib.sha256(tokens[n].encode()).hexdigest()) for n,node in [('alice','aaaaaaaaaa'),('bob','bbbbbbbbbb')]])
    cfg=pathlib.Path(tmp)/'server.json';cfg.write_text(json.dumps(config));os.chmod(cfg,0o600)
    opener=urllib.request.build_opener(urllib.request.ProxyHandler({}))
    def call(method,path,name='alice',body=None,expected=200):
        data=None if body is None else json.dumps(body).encode()
        req=urllib.request.Request(f'http://127.0.0.1:{port}/api/v1'+path,data=data,method=method,
          headers={'Authorization':'Bearer '+tokens[name],'Content-Type':'application/json'})
        try:
            with opener.open(req,timeout=3) as resp:status=resp.status;raw=resp.read()
        except urllib.error.HTTPError as e:status=e.code;raw=e.read()
        assert status==expected,(path,status,raw)
        return json.loads(raw) if raw else None
    proc=subprocess.Popen([binary,'--mock','--config',str(cfg)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    try:
        for _ in range(100):
            try:
                if call('GET','/health')['mode']=='mock':break
            except (OSError,AssertionError):time.sleep(.05)
        else:raise AssertionError('server not ready')
        call('POST','/rooms',body={'gameId':'dst','policy':'direct_required'},expected=422)
        room=call('POST','/rooms',body={'gameId':'dst'},expected=201)
        path='/rooms/'+room['id']
        call('GET',path,'bob',expected=403)
        call('POST',path+'/join','bob',{'joinKey':room['joinKey']})
        assert len(call('GET',path+'/peers','bob')['peers'])==2
        call('POST',path+'/leave',body={},expected=204)
        call('GET',path,expected=404)
        print('PASS real server process: health, strict rejection, create, ACL, join, peers, leave')
    finally:
        proc.terminate()
        try:proc.wait(timeout=5)
        except subprocess.TimeoutExpired:proc.kill();proc.wait()
