#!/bin/bash

function get_pem_file() {
	infile=$1
	level=$((1 + $2))
	if [ -z $infile ] 
	then
		return 1 
	fi
	echo Processing $1
	crt=`openssl x509 -in ${infile}.pem -noout -text |sed -n -e 's/.*CA Issu.*URI:http:\/\/.*\/\(.*\)\..*/\1/p'` 
	uri=`openssl x509 -in ${infile}.pem -noout -text |sed -n -e 's/.*CA Issu.*URI:\(.*\)/\1/p'`
	if [ -z $uri ]
        then
                return 1
        fi
	echo crt=$crt, uri=$uri
	wget ${uri}
	openssl x509 -inform der -in ${crt}.crt -out ${crt}.pem
	export outfile=$crt
	if [ $level -lt 5 ]
	then 
		if get_pem_file $outfile level; then
	        	echo completed $outfile with result $?
	        else
	                echo done
	        fi
	fi
	return 0
}

function get_all_pem(){
	export outfile=$2
	export url=$1
	openssl s_client -showcerts -connect ${url}:443 </dev/null 2>/dev/null|openssl x509 -outform PEM >${outfile}.pem
	get_pem_file $outfile 0
}


rm *.pem
rm *.crt
rm *.txt
# seed the start pem
get_all_pem github.com github-com
get_all_pem s3.amazonaws.com s3-amazon-com
get_all_pem github-releases.githubusercontent.com githubusercontent-com
cat *.pem >github.pem 

