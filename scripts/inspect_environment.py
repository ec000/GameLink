#!/usr/bin/env python3
"""Read-only inspection. Writes only the requested report; never elevates or mutates networking."""
import argparse, datetime, json, os, pathlib, platform, shutil, subprocess, urllib.request, urllib.error


def run(args):
    if not shutil.which(args[0]):
        return {'status': 'unavailable', 'command': args[0]}
    try:
        p = subprocess.run(args, capture_output=True, text=True, timeout=8)
        # stderr may include paths, but never requests token arguments.
        return {'status': 'ok' if p.returncode == 0 else 'failed', 'exit': p.returncode,
                'stdout': p.stdout[:32000], 'stderr': p.stderr[:2000]}
    except subprocess.TimeoutExpired:
        return {'status': 'timeout'}


def select(d, keys):
    return {k: d[k] for k in keys if k in d}


def inspect(zt_home=None):
    report = {'generatedAt': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'system': platform.platform(), 'mutations': [], 'zerotier': {}, 'checks': {}}
    report['checks']['uname'] = run(['uname', '-a'])
    for cmd in ['info', 'listnetworks', 'peers']:
        raw = run(['zerotier-cli', '-j', cmd])
        if raw.get('status') == 'ok':
            try:
                d = json.loads(raw['stdout'])
                if cmd == 'info':
                    d = select(d, ['address', 'version', 'online', 'tcpFallbackActive'])
                elif cmd == 'peers':
                    d = [select(p, ['address', 'role', 'latency', 'tunneled', 'paths']) for p in d]
                else:
                    d = [select(n, ['id', 'nwid', 'status', 'type', 'assignedAddresses', 'portDeviceName',
                                     'allowDefault', 'allowGlobal', 'allowDNS', 'routes']) for n in d]
                raw = {'status': 'ok', 'data': d}
            except (ValueError, TypeError):
                raw = {'status': 'unsupported_json'}
        else:
            raw = select(raw, ['status', 'exit'])  # Don't retain CLI error bodies.
        report['zerotier'][cmd] = raw
    if platform.system() == 'Linux':
        cmds = {'service': ['systemctl', 'status', '--no-pager', 'zerotier-one'],
                'listeners': ['ss', '-lunpt'], 'udp': ['ss', '-u', '-a', '-n', '-p'],
                'linkStats': ['ip', '-s', 'link'], 'routes4': ['ip', '-4', 'route', 'show'],
                'routes6': ['ip', '-6', 'route', 'show'], 'bridges': ['ip', '-d', 'link', 'show', 'type', 'bridge'],
                'forwarding': ['sysctl', 'net.ipv4.ip_forward', 'net.ipv6.conf.all.forwarding'],
                'nft': ['nft', 'list', 'ruleset'], 'iptables': ['iptables-save']}
        report['checks'].update({k: run(v) for k, v in cmds.items()})
        zt_home = zt_home or '/var/lib/zerotier-one'
    elif platform.system() == 'Darwin':
        zt_home = zt_home or '/Library/Application Support/ZeroTier/One'
    else:
        zt_home = zt_home or os.path.join(os.environ.get('PROGRAMDATA', 'C:/ProgramData'), 'ZeroTier/One')
    home = pathlib.Path(zt_home)
    report['zerotier']['homeExists'] = home.exists()
    for item in ['planet', 'moons.d', 'controller.d', 'local.conf']:
        try:
            path = home / item
            report['zerotier'][item] = {'exists': path.exists()}
            if item == 'moons.d' and path.is_dir():
                report['zerotier'][item]['files'] = [p.name for p in path.glob('*.moon')]
            if item == 'local.conf' and path.exists():
                data = json.loads(path.read_text())
                report['zerotier'][item]['settings'] = select(data.get('settings', {}),
                    ['forceTcpRelay', 'allowTcpFallbackRelay', 'tcpFallbackRelay', 'primaryPort'])
        except (OSError, ValueError):
            report['zerotier'][item] = {'status': 'unreadable_or_invalid'}
    # Read the token only into memory; fixed loopback target, no redirect, no proxy.
    class NoRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, *args, **kwargs):
            return None
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())
    try:
        token = (home / 'authtoken.secret').read_text().strip()
        port = int((home / 'zerotier-one.port').read_text().strip())
        if not 0 < port < 65536:
            raise ValueError('port')
        for path in ['/status', '/controller/network', '/moon']:
            try:
                req = urllib.request.Request(f'http://127.0.0.1:{port}{path}', headers={'X-ZT1-Auth': token})
                with opener.open(req, timeout=3) as response:
                    data = json.loads(response.read(1024 * 1024))
                if path == '/status':
                    data = select(data, ['version', 'address', 'online', 'tcpFallbackActive'])
                elif path == '/moon':
                    data = [select(m, ['id', 'waiting']) for m in data]
                report['zerotier'][path] = {'status': 'ok', 'data': data}
            except urllib.error.HTTPError as e:
                report['zerotier'][path] = {'status': 'http_error', 'code': e.code}
            except (OSError, ValueError, TypeError):
                report['zerotier'][path] = {'status': 'unavailable'}
    except (OSError, ValueError):
        report['zerotier']['localAPI'] = {'status': 'unavailable_token_or_port'}
    return report


def render(report):
    zt = report['zerotier']
    info = zt['info'].get('data', {})
    return '\n'.join(['# GameLink 部署只读评估', '', f"采集时间：{report['generatedAt']}",
        f"系统：{report['system']}", f"ZeroTier 版本：{info.get('version', 'UNKNOWN')}",
        f"Controller API：{zt.get('/controller/network', {}).get('status', 'UNKNOWN')}",
        f"TCP fallback 当前活动：{info.get('tcpFallbackActive', 'UNKNOWN')}", '',
        '未执行任何网络配置变更；无法读取的检查是 UNKNOWN，不能推断不存在。',
        'planet 文件存在无法判断是否自定义；需要管理员比对已安装发行版 hash。',
        'moons.d 与 /moon 只描述本机，不代表所有玩家的 roots。',
        '端口、转发、NAT、网桥与权限失败详情见下面 JSON；上线前人工审核。',
        '没有 SSH 信息，未连接任何远端 VPS；本地检测结果不能代替 VPS 评估。', '',
        '```json', json.dumps(report, ensure_ascii=False, indent=2), '```', ''])


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', default='environment-report.md')
    parser.add_argument('--zt-home')
    args = parser.parse_args()
    pathlib.Path(args.output).write_text(render(inspect(args.zt_home)), encoding='utf-8')
    print(f'Read-only report saved: {args.output}')
