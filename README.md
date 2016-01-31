# super-bootimg
Tools to edit Android boot.img. NDK buildable, to be usable in an update.zip

# Integrate superuser.zip into another zip

In your update.zip, create a folder called su, and put superuser.zip in it,
then add the following lines in your updater-script, AFTER flashing boot.img:

> package_extract_dir("su", "/tmp/su");
> run_program("/sbin/busybox", "unzip", "/tmp/su/superuser.zip", "META-INF/com/google/android/update-binary", "-d", "/tmp/su");
> run_program("/sbin/busybox", "sh", "/tmp/su/META-INF/com/google/android/update-binary", "3", "42", "/tmp/su/superuser.zip");
