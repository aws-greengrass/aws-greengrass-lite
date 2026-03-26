#!/bin/awk -f
BEGIN{
    uss = 0
    pss = 0
    rss = 0
}
{
    if(NF != 3) {
        ignore
    } else if($1 ~ /Private_(Clean|Dirty):/) {
        uss += $2;
    } else if($1 == "Pss:") {
        pss += $2;
    } else if($1 == "Rss:") {
        rss += $2;
    }
}
END{
    printf("%6s    %6s    %6s\n", "USS", "PSS", "RSS");
    printf("%6d kB %6d kB %6d kB\n\n", uss, pss, rss);
}
