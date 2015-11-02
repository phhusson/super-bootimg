#!/system/bin/sh

cat >> init.rc <<EOF

service host_mount mount -o bind /data/hosts /system/etc/hosts
	class main
	oneshot
EOF
addFile init.rc

allow init system_data_file "file" "mounton"

(echo 'ro.sf.lcd_density=320' ;cat default.prop) > t
mv -f t default.prop
addFile default.prop
