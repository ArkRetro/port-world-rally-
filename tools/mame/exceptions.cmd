bpset 536,1,{ printf "EXC buserr pc=%x\n",curpc ; g }
bpset 544,1,{ printf "EXC addrerr\n" ; g }
bpset 552,1,{ printf "EXC illegal\n" ; g }
bpset 55e,1,{ printf "EXC div0\n" ; g }
bpset 56a,1,{ printf "EXC chk\n" ; g }
bpset 576,1,{ printf "EXC priv\n" ; g }
bpset 582,1,{ printf "EXC 8\n" ; g }
bpset 400,1,{ printf "VEC400 default handler\n" ; g }
g
