"""Send a firmware image to a Brainwave module over its control port and wait for it to restart in the other slot."""

import argparse
import hashlib
import socket
import sys
import time


def request(host, port, command, timeout=10):
    with socket.create_connection((host, port), timeout=timeout) as connection:
        connection.sendall((command + '\n').encode())
        reply = b''
        while not reply.endswith(b'ok\n') and b'\nerror ' not in b'\n' + reply:
            chunk = connection.recv(4096)
            if not chunk:
                break
            reply += chunk
    return reply.decode(errors='replace')


def status(host, port):
    values = {}
    for line in request(host, port, 'status').splitlines():
        if '=' in line:
            key, value = line.split('=', 1)
            values[key] = value
    return values


def read_line(connection):
    line = b''
    while not line.endswith(b'\n'):
        chunk = connection.recv(1)
        if not chunk:
            break
        line += chunk
    return line.decode(errors='replace').strip()


def main():
    parser = argparse.ArgumentParser(description='Send a firmware image to a Brainwave module over its control port')
    parser.add_argument('host', help='module address: 10.88.0.1 from the Brain, or its WiFi address with a debug build')
    parser.add_argument('image', help='build/firmware/brainwave.bin')
    parser.add_argument('--port', type=int, default=2350)
    arguments = parser.parse_args()

    image = open(arguments.image, 'rb').read()
    digest = hashlib.sha256(image).hexdigest()
    before = status(arguments.host, arguments.port)
    print(f'module {before.get("version")} in slot {before.get("slot")}; sending {len(image)} bytes, SHA-256 {digest}')

    with socket.create_connection((arguments.host, arguments.port), timeout=30) as connection:
        connection.sendall(f'update {len(image)} {digest}\n'.encode())
        answer = read_line(connection)
        if answer != 'ready':
            sys.exit(f'push-firmware: module said {answer!r}')
        started = time.time()
        sent = 0
        while sent < len(image):
            chunk = image[sent:sent + 4096]
            connection.sendall(chunk)
            sent += len(chunk)
            print(f'\r{sent * 100 // len(image)}%', end='', flush=True)
        result = read_line(connection)
        print(f'\r{len(image)} bytes in {time.time() - started:.1f} s: {result}')
        if result != 'ok':
            sys.exit(1)

    print('waiting for the module to restart')
    deadline = time.time() + 90
    while time.time() < deadline:
        time.sleep(3)
        try:
            after = status(arguments.host, arguments.port, )
        except OSError:
            continue
        if after.get('slot') != before.get('slot'):
            print(f'module {after.get("version")} now running from slot {after.get("slot")}, uptime {after.get("uptime")} s')
            return
    sys.exit('push-firmware: the module did not come back in a different slot')


if __name__ == '__main__':
    main()
