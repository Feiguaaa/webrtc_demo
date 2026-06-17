"""
UDP Packet Loss Simulator - Transparent proxy with configurable packet loss rate.
Listens on a port, randomly drops packets, forwards the rest to target.
Uses asyncio for high throughput.
"""
import asyncio
import random
import sys
import os

class UDPLossProxy:
    def __init__(self, listen_host, listen_port, target_host, target_port, loss_rate):
        self.listen_host = listen_host
        self.listen_port = listen_port
        self.target_host = target_host
        self.target_port = target_port
        self.loss_rate = loss_rate
        self.packets_in = 0
        self.packets_dropped = 0
        self.packets_out = 0
        # Track connections by source address
        self.connections = {}  # (src_host, src_port) -> target_addr

    async def start(self):
        loop = asyncio.get_event_loop()
        # Create UDP socket
        transport, protocol = await loop.create_datagram_endpoint(
            lambda: ProxyProtocol(self),
            local_addr=(self.listen_host, self.listen_port)
        )
        print(f"[UDPProxy] Listening on {self.listen_host}:{self.listen_port}, "
              f"forwarding to {self.target_host}:{self.target_port}, "
              f"loss rate: {self.loss_rate*100:.1f}%")
        try:
            while True:
                await asyncio.sleep(1)
                total = self.packets_in
                if total > 0:
                    print(f"[UDPProxy] Stats: {total} in, "
                          f"{self.packets_dropped} dropped ({self.packets_dropped/total*100:.1f}%), "
                          f"{self.packets_out} forwarded")
        except KeyboardInterrupt:
            pass
        finally:
            transport.close()

class ProxyProtocol(asyncio.DatagramProtocol):
    def __init__(self, proxy):
        self.proxy = proxy
        self.transport = None

    def connection_made(self, transport):
        self.transport = transport

    def datagram_received(self, data, addr):
        self.proxy.packets_in += 1

        # Randomly drop packet
        if random.random() < self.proxy.loss_rate:
            self.proxy.packets_dropped += 1
            return

        # Forward to target
        self.proxy.packets_out += 1
        self.transport.sendto(
            data,
            (self.proxy.target_host, self.proxy.target_port)
        )

async def main():
    if len(sys.argv) < 5:
        print(f"Usage: {sys.argv[0]} <listen_port> <target_host> <target_port> <loss_rate>")
        print(f"  loss_rate: 0.0 to 1.0 (e.g., 0.1 = 10% loss)")
        sys.exit(1)

    listen_port = int(sys.argv[1])
    target_host = sys.argv[2]
    target_port = int(sys.argv[3])
    loss_rate = float(sys.argv[4])

    proxy = UDPLossProxy('127.0.0.1', listen_port, target_host, target_port, loss_rate)
    await proxy.start()

if __name__ == '__main__':
    asyncio.run(main())
