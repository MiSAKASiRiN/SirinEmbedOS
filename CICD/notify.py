#!/usr/bin/env python3
"""Feishu CI cards. Addresses exist only in job variables and the request body."""
import argparse
import base64
from datetime import datetime, timedelta, timezone
import hashlib
import hmac
import json
import os
from pathlib import Path
import sys
import time
import urllib.parse
import urllib.request

from common import redact


def address(value):
    parsed = urllib.parse.urlsplit(value)
    if parsed.scheme != 'https' or not parsed.hostname or parsed.username or parsed.password:
        raise ValueError('a valid HTTPS address without embedded credentials is required')
    return value.rstrip('/')


def card(summary, environment):
    success = summary.get('status') == 'success'
    color = 'green' if success else 'red'
    status = 'SUCCESS' if success else 'FAILED'

    def markdown(text):
        return {'tag': 'markdown', 'content': redact(text)}

    def columns(*items):
        return {'tag': 'column_set', 'flex_mode': 'stretch', 'horizontal_spacing': '12px',
                'columns': [{'tag': 'column', 'width': 'weighted', 'weight': 1, 'elements': [item]} for item in items]}

    checks = '\n'.join(f'{"PASS" if c["exit_code"] == 0 else "FAIL"}: {c["name"]}' for c in summary.get('checks', []))
    if not checks:
        checks = '没有收到构建摘要；任务可能在脚本启动前失败，请查看流水线。'
    memory = '\n\n'.join(m.get('configuration', '') + '\n' + m.get('text', '') for m in summary.get('memory', []))
    elements = [
        columns(markdown(f'**<font color="{color}">CI BUILD {status}</font>**\nBuild #{environment.get("CI_PIPELINE_ID", "")}'),
                markdown('**🕒 UTC+8**\n' + datetime.now(timezone(timedelta(hours=8))).strftime('%Y-%m-%d %H:%M:%S'))),
        columns(markdown('**REPO**\n' + environment.get('CI_PROJECT_PATH', '')),
                markdown('**Branch**\n' + environment.get('CI_COMMIT_REF_NAME', ''))),
        columns(markdown('**Commit by**\n' + environment.get('GITLAB_USER_NAME', '')),
                markdown('**Commit ID**\n' + environment.get('CI_COMMIT_SHORT_SHA', ''))),
        markdown('**Commit Message**\n' + environment.get('CI_COMMIT_MESSAGE', '')[:800]),
        markdown('**Build / Export**\n```text\n' + checks[:2500] + '\n```'),
    ]
    for column, background in zip(elements[0]['columns'], (color + '-50', 'blue-50' if success else 'orange-50')):
        column.update(background_style=background, padding='12px 12px 12px 12px')
    diagnostics = summary.get('diagnostics', '')
    if diagnostics:
        elements.append(markdown('**Compile Message**\n```text\n' + diagnostics[-2400:] + '\n```'))
    if memory:
        elements.append(markdown('**Memory**\n```text\n' + memory[:2200] + '\n```'))
    project = address(environment.get('CI_PROJECT_URL', ''))
    pipeline = address(environment.get('CI_PIPELINE_URL', ''))
    job_id = str(summary.get('build_job_id', ''))
    buttons = [('查看仓库', project), ('查看Pipeline', pipeline)]
    if job_id.isdecimal():
        buttons.append(('查看Artifacts', f'{project}/-/jobs/{job_id}/artifacts/browse'))
    elements.append(columns(*[{'tag': 'button', 'text': {'tag': 'plain_text', 'content': label},
        'type': 'primary_filled' if i == 0 else 'default',
        'behaviors': [{'type': 'open_url', 'default_url': url}]} for i, (label, url) in enumerate(buttons)]))
    return {'msg_type': 'interactive', 'card': {'schema': '2.0',
        'config': {'update_multi': True, 'style': {'text_size': {'normal_v2': {
            'default': 'normal', 'pc': 'normal', 'mobile': 'heading'}}}},
        'header': {'title': {'tag': 'plain_text', 'content': 'SirinEmbedOS UPDATE ' + status},
                   'template': color,
                   'text_tag_list': [{'tag': 'text_tag', 'text': {'tag': 'plain_text',
                       'content': 'OK' if success else 'ERROR'}, 'color': color}]},
        'body': {'direction': 'vertical', 'elements': elements}}}


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise ValueError('notification redirect refused')


def send(payload, webhook, secret=''):
    webhook = address(webhook)
    if secret:
        stamp = str(int(time.time()))
        key = f'{stamp}\n{secret}'.encode()
        payload = dict(payload, timestamp=stamp,
                       sign=base64.b64encode(hmac.new(key, digestmod=hashlib.sha256).digest()).decode())
    request = urllib.request.Request(webhook, data=json.dumps(payload, ensure_ascii=False).encode(),
                                     headers={'Content-Type': 'application/json'}, method='POST')
    with urllib.request.build_opener(NoRedirect()).open(request, timeout=10) as response:
        result = json.loads(response.read(65536))
    # Never print remote error bodies, exception messages or addresses.
    if result.get('code', result.get('StatusCode')) != 0:
        raise ValueError('notification rejected by remote service')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('ci-output'))
    parser.add_argument('--dry-run', action='store_true', help='Render redacted preview; no request')
    args = parser.parse_args()
    if not os.environ.get('FEISHU_WEBHOOK') and not args.dry_run:
        print('[CI] Notification skipped: FEISHU_WEBHOOK is not configured')
        return 0
    try:
        path = args.output / 'summary.json'
        summary = json.loads(path.read_text()) if path.exists() else {'status': 'failed'}
        diagnostics = []
        for log in sorted((args.output / 'logs').glob('*.log')):
            for line in log.read_text(errors='replace').splitlines():
                if any(word in line.lower() for word in ('error:', 'warning:', 'failed', 'assertion', 'undefined reference')):
                    diagnostics.append(redact(line))
        summary['diagnostics'] = '\n'.join(diagnostics[-50:])
        payload = card(summary, os.environ)
        if args.dry_run:
            print(redact(json.dumps(payload, ensure_ascii=False)))
        else:
            send(payload, os.environ['FEISHU_WEBHOOK'], os.environ.get('FEISHU_SIGNING_SECRET', ''))
            print('[CI] Feishu notification accepted')
        return 0
    except Exception as error:
        print(f'[CI] Notification failed ({type(error).__name__}); addresses and response bodies suppressed')
        return 1


if __name__ == '__main__':
    sys.exit(main())
