#/bin/sh
full_path=$(dirname "$0")
while read pid command
do 
    echo $command
    read user group < <(stat -c "%U %G" /proc/$pid/)
    sudo -u $user -g $group cat /proc/$pid/smaps_rollup | $full_path/rollup_memory.awk
done < <(pgrep -P 1 -a | grep /usr/local/bin/)
