#!/bin/bash

function get_local_crash_version ()
{
    local inc=extpy/inc
    which crash 1>/dev/null 2>/dev/null || return 1
    
    local v=$(crash -v | grep '^crash [0-9]' | \
        awk '{print $2}' | cut -d '-' -f 1)
    if [ ! -z "$v" ]; then
        if [ -f "$inc/$v/crash/defs.h" ]; then
            echo "$v"
        elif [ -f "/usr/include/crash/defs.h" ]; then
            echo "$v"
        else
            return 1
        fi
    fi
}

function is_cmd ()
{
    case "$1" in
        clean | install | uninstall )
            return 0 ;;
        * )
            return 1 ;;
    esac
}

function usage ()
{
    echo "usage:"
    echo "       build.sh <crash_version>"
    echo "       build.sh [<crash_version>] <clean|install|uninstall>"
    echo ""
    
    echo "crash_version:"
    local v=$(get_local_crash_version)
    if [ ! -z "$v" ]; then
        echo "       local -- $v"
    fi
    
    local cnt=0
    local inc=extpy/inc
    for v in $(ls $inc/); do
        if [[ "$v" =~ ".zip" ]]; then
            continue
        fi
        
        if (( cnt % 4 == 0 )); then
            echo -en "       $v"
        elif (( cnt % 4 == 3 )); then
            echo -e "    $v"
        else
            echo -n "    $v"
        fi
        (( cnt++ ))
    done
    echo
}

function get_cpu_type ()
{
    unset CPU_TYPE
    
    local type=$(uname -p)
    if [ "$type" == "x86_64" ]; then
        export CPU_TYPE="X86_64"
    elif [ "$type" == "aarch64" ]; then
        export CPU_TYPE="ARM"
    else
        echo "Error: *** unknown cpu-type $type."
        exit 1
    fi
}

function check_crash_version ()
{
    local local_version=$(get_local_crash_version)
    local inc="extpy/inc"
    
    local version=$1
    if [ -z "$version" -o "$version" == "local" ]; then
        version=$local_version
    fi
    
    unset CRASH_VERSION
    export CRASH_VERSION=$version
    
    if [ -z "$CRASH_VERSION" ]; then
        echo "Error: *** crash_version is null."
        exit 1
    fi
    
    [ "$local_version" == "$CRASH_VERSION" ] &&
    [ -f "/usr/include/crash/defs.h" ] && return
    
    if [ ! -f "$inc/$CRASH_VERSION/crash/defs.h" -a -f '$inc/inc.zip' ]; then
        unzip -o $inc/inc.zip -d $inc/ || exit 1
    fi
    
    if [ ! -f "$inc/$CRASH_VERSION/crash/defs.h" ]; then
        echo "Error: *** $inc/$CRASH_VERSION/crash/defs.h does not exist."
        exit 1
    fi
}

function init_env ()
{
    get_cpu_type
    check_crash_version $1
}

function build_extpy ()
{
    make $@ -C extpy/src/ CRASH_CPU_TYPE=$CPU_TYPE CRASH_VERSION=$CRASH_VERSION
}

function build_extmm ()
{
    if [ -f extmm/inc/auto/zfs_crash.h ]; then
        export COMPILE_DEMO=y
    fi
    
    make $@ -C extmm CRASH_CPU_TYPE=$CPU_TYPE CRASH_VERSION=$CRASH_VERSION
}

function build ()
{
    local inc=extpy/inc
    ls $inc/*.*.* 1>/dev/null 2>&1 || {
        [ -f $inc/inc.zip ] && unzip -o $inc/inc.zip -d $inc/ 1>/dev/null
    }
    
    if [ $# -eq 0 ]; then
        usage
        return
    fi
    
    is_cmd "$1"
    if [ $? -eq 0 ]; then
        init_env local
    else
        init_env "$1"; shift
    fi
    
    build_extpy $@ && build_extmm $@
}

build $@
