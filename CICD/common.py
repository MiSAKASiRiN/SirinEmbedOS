"""Host-side CI helpers; never persist deployment addresses or credentials."""
import os
import re


def redact(value):
    text = str(value)
    # Remove terminal controls before matching, including coloured diagnostics.
    text = re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]', '', text)
    for key, secret in sorted(os.environ.items(), key=lambda item: -len(item[1])):
        if secret and any(word in key.upper() for word in ('TOKEN', 'PASSWORD', 'SECRET', 'WEBHOOK')):
            text = text.replace(secret, '[REDACTED]')
    for key in ('CI_PROJECT_DIR', 'SRNOS_CI_SOURCE_ROOT', 'SRNOS_CI_SDK_ROOT'):
        path = os.environ.get(key, '')
        if path:
            text = text.replace(path.rstrip('/') + '/', '')
    text = re.sub(r'\b(?:https?|ssh)://[^\s<>"\x27]+', '[URL]', text, flags=re.I)
    text = re.sub(r'\b[\w.+-]+@[\w.-]+:[^\s<>"\x27]+', '[SSH_ADDRESS]', text)
    text = re.sub(r'\b(?:\d{1,3}\.){3}\d{1,3}\b', '[IP]', text)
    return text
