#!/bin/bash

sudo mn --controller remote,port=6653 --switch ovsk,protocols=OpenFlow13 --custom ./topo.py --topo mytopo
