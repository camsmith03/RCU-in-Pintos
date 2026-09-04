#!/bin/bash 

# PINTOS test runner to simplify having to enter in commands 
#
# made by Cameron Smith

# CHANGE THIS VARIABLE TO SELECT WHICH FOLDER TESTS SHOULD BE RAN FROM! 
test_dir="filesys"

function print_usage {
  echo "Usage: ptest <test> [OPTIONS]"
  echo "       ptest --find <matching-test>"
  echo "       ptest --set-src /location/of/pintos-fork/src"
  echo ""
  echo "OPTIONS:  --gdb     :  run with debugger"
  echo "          --smp N   :  run with N cpus"
  echo "          --qemu    :  run with qemu (default, use w/ gdb)"
  echo "          --kvm     :  run with kvm"
  echo "          --bochs   :  run with bochs"
  echo "          --dry-run :  only show command"
  echo "          --find    :  find partial matches to some test"
  echo "          --check   :  run using make check instead of ptest mode"
  echo "          --set-src :  set the src dir for pintos-fork (stays persistant)"
  echo "          --mem N   :  run with N KB of memory"
  echo "          --help    :  see this menu"
  exit
}

# See if the input is a non-zero number
function is_a_number {
  if ! [[ "$1" =~ "^[0-9]+$" ]]; then
    if [[ "$1" -ne "0" ]]; then 
      echo 1
    fi 
  else 
    echo 0
  fi 
}

# Used for the '--find' option to find the right files 
function find_test {
  tests=$(find . -iwholename "*$1*" | grep -v "result\|output\|errors\|\.o\|\.d|\.tar")
    
  if [[ -n $tests ]]; then 
    echo "$tests"
    exit 
  fi 

  echo "No tests found with partial match to: $1"
  exit
}

function src_dir_not_found {
  echo "==============================================="
  echo "!!!! PINTOS SRC DIRECTORY HAS NOT BEEN SET !!!!"
  echo "==============================================="
  echo ""
  echo "To set it permanently, run the following: "
  echo ""
  echo "$> ptest --set-src /location/of/pintos-fork/src"
  echo ""
  echo "==============================================="
  exit 
}

# Used for the '--set-src' option 
if [[ -n $(echo "$1" | grep "set-src") ]]; then 
  if [[ -z "$2" ]] || ! [[ -f "$2/Makefile.kernel" ]]; then 
    echo "Directory not found: $2"
    exit 
  fi 
  abs_path=$(readlink -m "$2")
  
  # Add the symbol to the rc file 
  echo "export PINTOS_FORK_DIR=$abs_path" > ~/.ptestrc
  chmod +x ~/.ptestrc 
  exit 
fi 

if [ ! -f ~/.ptestrc ]; then 
  src_dir_not_found 
fi 

if [[ -z $(cat ~/.ptestrc | grep "PINTOS_FORK_DIR") ]]; then 
  src_dir_not_found
fi 

source ~/.ptestrc >& /dev/null

# See if the cwd is the build directory 
if [[ "$(echo $(pwd) | tail -c 6)" != "build" ]]; then
  cd "$PINTOS_FORK_DIR/$test_dir" 
  make_output=$(make -j$(nproc) | grep "Error")
  if [[ -n "$make_output" ]]; then 
    echo "Make failed!!"
    exit 
  fi 
  cd ./build 
fi 

# Ensure that a test was passed 
if [[ "$#" -lt 1 ]]; then 
  print_usage
fi 

test="$1"
persist_test=""

# Trim persistence tests and set the relevant flag 
if [[ -n $(echo $test | grep "persistence") ]]; then 
  persist_test="yes"
  test="$(echo $test | sed 's/.\{12\}$//')"
fi 

base_cmd="pintos -v -k"
args=""
emulator=""
dry_run=""
make_check=""

# Parses an option (w/ or w/o suboptions) and appends it to the args variable 
function parse_option {
  if [[ -n $(echo $args | grep -- "$1") ]]; then 
    echo "ignoring repeated option..."
  else 
    if [[ $1 = "--gdb" ]]; then 
      args="$args --gdb"
    elif [[ $1 = "--smp" ]]; then  
      num_output=$(is_a_number "$2")
      if [[ $num_output = "1" ]]; then 
        args="$args --smp $2"
      else 
        exit 
        print_usage
      fi 
    elif [[ $1 = "--mem" ]]; then  
      args="$args -m $2"
    else 
      if [[ $1 = "--kvm" ]]; then 
        if [[ -n $(echo $emulator | grep 'qemu') ]]; then 
          echo "You can't run kvm and qemu at the same time"
          echo ""
          print_usage
        fi 
        emulator="--kvm"
      elif [[ $1 = "--qemu" ]]; then 
        emulator="--qemu"
      elif [[ $1 = "--bochs" ]]; then 
        emulator="--bochs"
      elif [[ $1 = "--help" ]] || [[ $1 = "-h" ]]; then 
        print_usage 
      elif [[ $1 = "--dry-run" ]]; then 
        dry_run="yes"
      elif [[ $1 = "--check" ]]; then 
        make_check="yes"
      else 
        echo "Unsupported option: $1"
        echo ""
        print_usage 
      fi 
    fi 
  fi 
}

if [[ "$test" = "--find" ]]; then
  if [[ "$#" = "2" ]]; then
    find_test "$2"
  fi 

  print_usage 
fi 

# Find the path to the test in the cwd 
test_loc=$(find . -regex ".*/$test$")

if [[ -z "$test_loc" ]]; then 
  echo "== Test $test not found! =="
  echo ""
  print_usage 
fi 

# Populate args variable 
for ((i = 2, j = 3; i <= $#; i++, j++)); do   
  if [[ $i -eq $# ]]; then 
    parse_option "${!i}"
  else 
    if [[ "${!i}" = "--smp" ]]; then 
      # pass $i and $(i+1) to parse_option, then skip to i += 2  
      parse_option "${!i}" "${!j}"
      i=$((i+1))
      j=$((j+1))
    elif [[ "${!i}" = "--mem" ]]; then 
      # pass $i and $(i+1) to parse_option, then skip to i += 2  
      parse_option "${!i}" "${!j}"
      i=$((i+1))
      j=$((j+1))
    else 
      parse_option "${!i}"
    fi 
  fi 
done 

if [[ -z "$emulator" ]]; then 
  emulator="--qemu" # default emulator 
fi 

if [[ -n "$(echo $emulator | grep 'kvm')" ]]; then 
  if [[ -n "$(echo $args | grep 'gdb')" ]]; then 
    echo "=== Warning, using kvm with gdb is not a good idea ==="
    echo ""
  fi
fi


# Remove the last run of the test if it exists
rm "$test_loc.result" >& /dev/null
rm "$test_loc.output" >& /dev/null
rm "$test_loc.errors" >& /dev/null

if [[ "$make_check" = "yes" ]]; then 
  make "$test_loc.result"
  exit 
fi 


test_result="$(make $test_loc.result --dry-run | grep -v echo | grep pintos)"
echo "$test_result" > ptest-test_res.tmp

# Grab the fist line of output from test_result. If this is for p4, this should 
# be pintos-mkdisk. Save it to the tmp file (to be deleted after)
p4_special_test="$(grep -m1 'pintos-mkdisk' ./ptest-test_res.tmp)"


# see if the test is for p4 involving disk creation 
if [[ -n $p4_special_test ]]; then 
  # remove the old disk if it exists 
  
  # Only delete previous disks if this isn't a persistence test 
  if [[ -z $persist_test ]]; then 
    rm *.dsk >& /dev/null
  fi 

  # If the command is not a dry run 
  if [[ -z  $dry_run ]]; then 

    if [[ -n $persist_test ]]; then 
      eval "$p4_special_test" # make the disk 
    else 
      # Gets the disk name from the pintos-mkdisk cmd 
      dsk_name=$(echo $p4_special_test | awk '{print $2}')
      
      # for persistence tests, only make the disk if it doesnt exist 
      if ! [ -f "$dsk_name" ]; then 
        eval "$p4_special_test"
      fi 
    fi 
  else 
    # Dry run output for p4 tests 
    echo "Make the disk (before each run if not persistence):"
    echo "$p4_special_test"
    echo ""
    echo "Run the test:"
    if [[ -n $persist_test ]]; then 
      echo "$(sed -n '2p' ./ptest-test_res.tmp)"
      echo ""
      echo "Save the tar:"
      echo "$(sed -n '3p' ./ptest-test_res.tmp)"
      echo ""
      # Remove the temp file
      rm ./ptest-test_res.tmp
      exit
    fi 
  fi  
  # Strip the first command (pintos) and use below parsing to continue
  test_result="$(sed -n '2p' ./ptest-test_res.tmp)"
fi

# Remove the temp file
rm ./ptest-test_res.tmp

if [[ -z $(echo $test_result | grep 'pintos') ]]; then 
  test_result="$(make $test_loc.result)"

  if [[ -z "$(echo $test_result | grep 'FAIL')" ]]; then 
    echo "Test $test is already passing"
    exit 
  fi 

  if [[ -z $(echo $test_result | grep 'pintos') ]]; then 
    echo "Sorry, perl script only tests are not supported..."
    exit
  fi
fi 


# Add the default smp value 
if [[ -z "$(echo $args | grep 'smp')" ]]; then 
  # Obtain the CPU count from (1-9)
  # cpu_cnt=$(echo $test_result | sed -n "s/.*smp\ \(.\).*/\1/p")
  cpu_cnt="8"
  # Append it to the args variable 
  # args="$args -smp $cpu_cnt,cores=$cpu_cnt,sockets=$cpu_cnt,threads=$cpu_cnt"
  
  # for i in $(seq 1 "$cpu_cnt"); do
  #   args="$args -vcpu vcpunum=$((i - 1)),affinity=$((i - 1))"
  # done
  

fi 

# test_result = pintos -k -v -T <time> --smp N --kvm ... < /dev/null ... FAIL  
# stripping this:            |---------------------|     |------------------->                    

# Strip off post FAIL 
test_result="${test_result%FAIL*}"

# Strip off everything after '< /dev/null' (if it exists)
test_result="${test_result%<*}"

prefix_cmd="../../utils/pintos -v -k" # pintos -v -k
suffix_cmd="${test_result#*--kvm}" # --filesys-size=2 ...

full_cmd="$prefix_cmd $args $emulator $suffix_cmd"
full_cmd=$(echo "$full_cmd" | awk '{$1=$1};1')

if [[ -z "$full_cmd" ]]; then 
  echo "Script failed..."
  exit 
fi 

if [[ -n $dry_run ]]; then 
  printf '%s\n' "$full_cmd" | fold 
  exit 
fi 

# No other arguments provided:
if [[ "$#" -eq 1 ]]; then 
  echo "Running test... (press Q to exit live output mode)"
  # just run the make script and cat the result file 
  echo "make $test_loc.result"
  make "$test_loc.result"  >& /dev/null &
  sleep 1
  end_output="Powering off..."
  while true; do 
    read -t 0.25 -N 1 exit_key
    test_output=$(cat "$test_loc.output" | fold -s)

    if [[ "$exit_key" = "q" ]] || [[ "$exit_key" = "Q" ]] || [[ -n $(echo "$test_output" | grep "$end_output") ]]; then 
      echo 
      break
    fi  
    echo "$test_output"
    sleep 0.1
  done 

  cat "$test_loc.output" | fold -s 
  
  echo ""
  echo "Waiting for result file..."
  echo ""
  while [ ! -f "$test_loc.result" ]; do 
    sleep 0.1
  done 

  echo ""
  echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
  echo "           CONTENTS OF $(echo $test_loc.result) "
  echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
  echo ""
  cat "$test_loc.result" | fold -s
  exit 
fi 

clear 
eval "$full_cmd"
exit
