[ -z "$PS1" ] && return
PS1='\w \$ '
HISTSIZE=500
HISTFILESIZE=500
HISTCONTROL=ignoredups
shopt -s histappend checkwinsize
. /etc/shrc

_jolt() {
    local word=${COMP_WORDS[COMP_CWORD]} choices=
    if [ "$COMP_CWORD" -eq 1 ]; then
        choices="fs win dm lm font face fonts bold theme themes mount umount open battery shortcut hotkeys keys latency serial-shell help"
    elif [ "$COMP_CWORD" -eq 2 ]; then
        case ${COMP_WORDS[1]} in
            theme)
                choices=classic
                local file
                for file in /usr/share/themes/*.conf; do
                    file=${file##*/}
                    file=${file%.conf}
                    file=${file%-dark}
                    file=${file%-light}
                    [[ " $choices " == *" $file "* ]] || choices="$choices $file"
                done
                ;;
            hotkeys) choices="off fullscreen always" ;;
            bold) choices="on off" ;;
            font) choices="8 10 12 14 16 18 20 24 28 32" ;;
            face)
                choices=default
                local file
                for file in /usr/share/fonts/*.ttf /usr/share/fonts/*.jbf; do
                    file=${file##*/}
                    file=${file%%[-.]*}
                    [[ " $choices " == *" $file "* ]] || choices="$choices $file"
                done
                ;;
        esac
    fi
    [ -n "$choices" ] && COMPREPLY=($(compgen -W "$choices" -- "$word"))
}
complete -o default -F _jolt jolt

_jpm() {
    local word=${COMP_WORDS[COMP_CWORD]} choices=
    if [ "$COMP_CWORD" -eq 1 ]; then
        choices="update list installed search info install remove upgrade files help"
    else
        case ${COMP_WORDS[1]} in
            install|info) [ -r /var/lib/jpm/index ] && choices=$(awk '$1 == "pkgname" { print $3 }' /var/lib/jpm/index) ;;
            remove|files)
                local directory
                for directory in /var/lib/jpm/*/; do
                    [ -r "$directory/PKGINFO" ] && directory=${directory%/} && choices="$choices ${directory##*/}"
                done
                ;;
        esac
    fi
    [ -n "$choices" ] && COMPREPLY=($(compgen -W "$choices" -- "$word"))
}
complete -o default -F _jpm jpm
