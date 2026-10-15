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
        choices="fs win dm lm font face fonts theme mount umount open battery shortcut hotkeys keys serial-shell help"
    elif [ "$COMP_CWORD" -eq 2 ]; then
        case ${COMP_WORDS[1]} in
            theme) choices="classic solarized" ;;
            hotkeys) choices="off fullscreen always" ;;
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
