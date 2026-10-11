wpset 10f300,d00,r,curpc>=c00000,{ printf "BIOSR addr=%x pc=%x\n",wpaddr,curpc ; g }
wpset 10f300,d00,w,curpc>=c00000,{ printf "BIOSW addr=%x pc=%x\n",wpaddr,curpc ; g }
g
