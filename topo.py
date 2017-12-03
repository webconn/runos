#!/usr/bin/env python

from mininet.topo import Topo


class MyTopo(Topo):
    " Topology 4 from task 4 "

    def __init__(self):

        Topo.__init__(self)

        # local hosts, before NAT
        local = [self.addHost('h%d' % i, ip="10.0.0.%d/16" % i) for i in range(1, 4)]

        # remote hosts, after NAT
        remote = [self.addHost('h%d' % i, ip="10.1.0.%d/16" % (i-3)) for i in range(4, 7)]

        # local switch
        localSwitch = self.addSwitch('s1')

        # NAT switch
        natSwitch = self.addSwitch('s2')

        # remote root switch
        remoteRootSwitch = self.addSwitch('s3')

        # remote top switch
        remoteTopSwitch = self.addSwitch('s4')
        remoteBottomSwitch = self.addSwitch('s5')

        # add links
        for h in local:
            self.addLink(h, localSwitch)

        self.addLink(localSwitch, natSwitch)
        self.addLink(natSwitch, remoteRootSwitch)

        self.addLink(remoteRootSwitch, remoteTopSwitch)
        self.addLink(remoteTopSwitch, remote[0])

        self.addLink(remoteRootSwitch, remote[1])

        self.addLink(remoteRootSwitch, remoteBottomSwitch)
        self.addLink(remoteBottomSwitch, remote[2])


topos = {'mytopo': (lambda: MyTopo())}
