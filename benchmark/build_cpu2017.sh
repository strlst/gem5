#!/bin/bash -x

CONFIG=rv64-linux

# SPECCPU2017_DIR must be set externaly
[ -z "${SPECCPU2017_DIR}" ] && echo "variables SPECCPU2017_DIR must be set when running this script" && exit 1

cd ${SPECCPU2017_DIR}
source shrc
runcpu --config ${CONFIG} --action runsetup --define bits=64 --size test --tune=base intrate fprate
