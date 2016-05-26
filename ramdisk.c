#define FUSE_USE_VERSION 30

#include <stdio.h>
#include <string.h>
#include <fuse.h>
#include <errno.h>
#include <time.h>
#include <limits.h>
#include "hashmap.h"

#define FILENAME_SIZE 100

long fs_size = 0;
long available_size = 0;
int load_fs = 0;
char* fs_path;

int IS_FILE = 0;
int IS_DIRECTORY= 1;

/*
	# Data Structure to store filesystem objects
*/
typedef struct fs_data_structure
{
	char* fs_object_name;
	int fs_object_type;
	struct stat* fs_object_details;

	struct fs_data_structure* parent_directory;

	// if fs_object is a file
	char* fs_object_content;

	// if fs_object is a directory
	map_t fs_object_map; // map of directories and files present in current directory
}fs_object;

fs_object *root;

/*
	# Checks if given path exists
	# Returns pointer to fs_object if given path exists else return NULL
*/
fs_object *get_fs_object(char *path)
{
    printf("Get fs_object at path : %s\n", path);
    int path_length = strlen(path);
	char tmp_path[path_length];
	strcpy(tmp_path, path);
	char* path_token;
	fs_object* fs_object_ptr = NULL;
	if(strcmp(path, "/") == 0)
	{
		fs_object_ptr = root;
	}
	else
	{
		path_token = strtok(tmp_path,"/");
		fs_object_ptr = root;
		fs_object* fs_object_tmp;
		while(path_token != NULL)
		{
            int msg = hashmap_get(fs_object_ptr->fs_object_map, path_token, (void**)(&fs_object_tmp));
            if(msg == MAP_OK)
            {
                fs_object_ptr = fs_object_tmp;
				path_token = strtok(NULL, "/");
            }
            else
            {
                fs_object_ptr = NULL;
                break;
            }
        }
	}
    printf("Exit get fs_object function\n");
	return fs_object_ptr;
}

/*
	# Checks if given file exist
	# Returns 0 if file exist else return error
	# We do not need to actually open the file, just return 0 is enough
*/
static int ramdisk_open(const char *path, struct fuse_file_info *fi)
{
    printf("ramdisk_open path : %s\n", path);
    int result = 0;
    fs_object* fs_object_ptr = get_fs_object(path);
    if (fs_object_ptr == NULL) 
    {
        result = -ENOENT;
    }
    printf("Exit ramdisk_open\n");
    return result;
}

/*
	# Reads the file at given path
*/
static int ramdisk_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    printf("ramdisk_read path : %s\n", path);
    int result = 0;
    fs_object *fs_object_ptr = get_fs_object(path);
    if(fs_object_ptr != NULL)
    {
        if(fs_object_ptr->fs_object_type == IS_FILE)
        {
            size_t content_size = fs_object_ptr->fs_object_details->st_size;
            if(offset < content_size)
            {
                if(offset + size > content_size)
                {
                    size = content_size - offset;
                }
                memcpy(buf, fs_object_ptr->fs_object_content + offset, size);
            }
            else
            {
                size = 0;
            }

            result = size;
        }
        else
        {
            result = -EISDIR;
        }
    }
    else
    {
        result = -ENOENT;
    }
    printf("Exit ramdisk_read\n");
    return result;
}

/*
	# Writes given data to file at given path
*/
static int ramdisk_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    printf("ramdisk_write path : %s\n", path);
    if(available_size < size)
    {
        return -ENOSPC;
    }
    int result = size;
    fs_object *fs_object_ptr = get_fs_object(path);
    if(fs_object_ptr != NULL)
    {
        if(fs_object_ptr->fs_object_type == IS_FILE)
        {
        	if(size > 0)
        	{
        		size_t content_size = fs_object_ptr->fs_object_details->st_size;
	        	if(content_size == 0) // write from the beginning of the file
	            {
	                fs_object_ptr->fs_object_content = (char *)malloc(sizeof(char) * size);
	                offset = 0;
	                memcpy(fs_object_ptr->fs_object_content + offset, buf, size);
	                fs_object_ptr->fs_object_details->st_size = offset + size;

	                // change modified time
	                time_t current_time;
	                time(&current_time);
	                fs_object_ptr->fs_object_details->st_mtime = current_time;
	                
	                // update available size
	                available_size = available_size - size;

	                result = size;
	            }
	            else if(content_size != 0) // append new contents to file
	            {
	                if(offset > content_size)
	                {
	                    offset = content_size;
	                }
	                long new_size = offset + size;
	                char *new_content = (char *)realloc(fs_object_ptr->fs_object_content, sizeof(char) * new_size);
	                fs_object_ptr->fs_object_content = new_content;
                    memcpy(fs_object_ptr->fs_object_content + offset, buf, size);
                    fs_object_ptr->fs_object_details->st_size = new_size;
                    
                    // change access time and modified time
                    time_t current_time;
                    time(&current_time);
                    fs_object_ptr->fs_object_details->st_mtime = current_time;

                    // update available size
                    available_size = available_size + content_size - new_size;
                    
                    result = size;
	            }
	        }
        }
        else
        {
            result = -EISDIR;
        }
    }
    else
    {
        result = -ENOENT;
    }
    printf("Exit ramdisk_write\n");
    return result;
}

/*
	# Deletes a file at given path
*/
static int ramdisk_unlink(const char *path)
{
    printf("ramdisk_unlink path : %s\n", path);
    int result = 0;
    fs_object *fs_object_ptr = get_fs_object(path);
    if(fs_object_ptr != NULL)
    {
        fs_object *fs_object_parent_ptr = fs_object_ptr->parent_directory;
        size_t old_size = fs_object_parent_ptr->fs_object_details->st_size;
        long updated_size = old_size;
        int msg = hashmap_remove(fs_object_parent_ptr->fs_object_map, fs_object_ptr->fs_object_name);
        if(fs_object_ptr->fs_object_details->st_size != 0)
        {
            available_size = available_size + fs_object_ptr->fs_object_details->st_size;
            updated_size = updated_size - fs_object_ptr->fs_object_details->st_size;
            free(fs_object_ptr->fs_object_content);
        }
        free(fs_object_ptr->fs_object_name);
        free(fs_object_ptr->fs_object_details);
        free(fs_object_ptr);

        long size_of_file = sizeof(fs_object) + sizeof(struct stat); // size required to store info about file

        updated_size = updated_size - size_of_file;
        if(updated_size < 0)
        	updated_size = 0;
        fs_object_parent_ptr->fs_object_details->st_size = updated_size;

        available_size = available_size + size_of_file;
    }
    else
    {
        result = -ENOENT;
    }
    printf("Exit ramdisk_unlink\n");
    return result;
}

/*
	# Creates a file at given path in given mode
*/
static int ramdisk_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    printf("ramdisk_create path : %s\n", path);
    int path_length = strlen(path);
	char tmp_path[path_length];
	strcpy(tmp_path, path);
	char dir_path[path_length];
	char* last_slash = strrchr(tmp_path, '/');
	char* file_name = last_slash + 1;
	*last_slash = 0;
	if(strlen(tmp_path) == 0)
    {
        strcpy(dir_path, "/");
    }
    else
    {
        strcpy(dir_path, tmp_path);
    }
	fs_object* fs_object_ptr = get_fs_object(dir_path);
    if(fs_object_ptr != NULL)
    {
    	fs_object* fs_object_tmp;
    	int msg = hashmap_get(fs_object_ptr->fs_object_map, file_name, (void**)(&fs_object_tmp));
        if(msg == MAP_OK)
        {
            return -EEXIST;
        }
    	else if(available_size < 0)
	    {
	        return -ENOSPC;
	    }
        else
        {
        	// create new file structure
        	fs_object *fs_object_new = (fs_object *)malloc(sizeof(fs_object));
	        fs_object_new->fs_object_details = (struct stat *)malloc(sizeof(struct stat));
	        fs_object_new->fs_object_name = malloc(FILENAME_SIZE * sizeof(char));
	        strcpy(fs_object_new->fs_object_name, file_name);

	        long size_of_file = sizeof(fs_object) + sizeof(struct stat); // size required to store info about file
            
            fs_object_new->fs_object_details->st_mode = S_IFREG | mode;
            fs_object_new->fs_object_details->st_nlink = 1;
            fs_object_new->fs_object_details->st_size = 0;
            
            time_t current_time;
            time(&current_time);
            fs_object_new->fs_object_details->st_mtime = current_time;
            fs_object_new->fs_object_details->st_ctime = current_time;
            
            fs_object_new->parent_directory = fs_object_ptr;
            fs_object_new->fs_object_map = hashmap_new();
            fs_object_new->fs_object_content = NULL;
            fs_object_new->fs_object_type = IS_FILE;
            
            // add new file to parent
            if(fs_object_ptr->fs_object_map == NULL)
            {
                fs_object_ptr->fs_object_map = hashmap_new();
            }
            int msg = hashmap_put(fs_object_ptr->fs_object_map, fs_object_new->fs_object_name, fs_object_new);

            size_t old_size = fs_object_ptr->fs_object_details->st_size;
            long updated_size = old_size + size_of_file;
            fs_object_ptr->fs_object_details->st_size = updated_size;

            available_size = available_size - size_of_file;
        }
    }
    else
    {
        return -ENOENT;
    }
    printf("Exit ramdisk_create\n");
    return 0;
}

/*
	# Creates a directory at given path in given mode
*/
static int ramdisk_mkdir(const char *path, mode_t mode)
{
    printf("ramdisk_mkdir path : %s\n", path);
    int path_length = strlen(path);
	char tmp_path[path_length];
	strcpy(tmp_path, path);
	char dir_path[path_length];
	char* last_slash = strrchr(tmp_path, '/');
	char* dir_name = last_slash + 1;
	*last_slash = 0;
	if(strlen(tmp_path) == 0)
    {
        strcpy(dir_path, "/");
    }
    else
    {
        strcpy(dir_path, tmp_path);
    }
	fs_object* fs_object_ptr = get_fs_object(dir_path);
    if(fs_object_ptr != NULL)
    {
    	fs_object* fs_object_tmp;
    	int msg = hashmap_get(fs_object_ptr->fs_object_map, dir_name, (void**)(&fs_object_tmp));
        if(msg == MAP_OK)
        {
            return -EEXIST;
        }
        else if(available_size < 0)
        {
            return -ENOSPC;
        }
        else
        {
        	// creating new dir structure
            fs_object *fs_object_new = (fs_object *)malloc(sizeof(fs_object));
            fs_object_new->fs_object_details = (struct stat *)malloc( sizeof(struct stat) );
        	fs_object_new->fs_object_name = malloc(FILENAME_SIZE * sizeof(char));
        	strcpy(fs_object_new->fs_object_name, dir_name);

            long size_of_dir = sizeof(fs_object) + sizeof(struct stat); // size required to store info about dir

            fs_object_new->fs_object_details->st_nlink = 2;
            fs_object_new->fs_object_details->st_mode = S_IFDIR | mode;
            fs_object_new->fs_object_details->st_size = size_of_dir;
            
            time_t current_time;
            time(&current_time);
            fs_object_new->fs_object_details->st_mtime = current_time;
            fs_object_new->fs_object_details->st_ctime = current_time;
            
            fs_object_new->parent_directory = fs_object_ptr;
            fs_object_new->fs_object_map = hashmap_new();
            fs_object_new->fs_object_type = IS_DIRECTORY;

            // adding new dir to parent
            if(fs_object_ptr->fs_object_map == NULL)
            {
                fs_object_ptr->fs_object_map = hashmap_new();
            }
            int msg = hashmap_put(fs_object_ptr->fs_object_map, fs_object_new->fs_object_name, fs_object_new);

            size_t old_size = fs_object_ptr->fs_object_details->st_size;
            long updated_size = old_size + size_of_dir;
            fs_object_ptr->fs_object_details->st_size = updated_size;

            available_size = available_size - size_of_dir;
        }
    }
    else
    {
        return -ENOENT;
    }
    printf("Exit ramdisk_mkdir\n");
    return 0;
}

/*
	# Removes the directory at given path
*/
static int ramdisk_rmdir(const char *path)
{
    printf("ramdisk_rmdir path : %s\n", path);
    fs_object *fs_object_ptr = get_fs_object(path);
    if(fs_object_ptr != NULL)
    {
       if(fs_object_ptr->fs_object_map != NULL && hashmap_length(fs_object_ptr->fs_object_map) > 0)
        {
            return -ENOTEMPTY;
        }
        fs_object *fs_object_parent_ptr = fs_object_ptr->parent_directory;
        int msg = hashmap_remove(fs_object_parent_ptr->fs_object_map, fs_object_ptr->fs_object_name);
        fs_object_parent_ptr->fs_object_details->st_nlink--;
        free(fs_object_ptr->fs_object_name);
        free(fs_object_ptr->fs_object_details);
        hashmap_free(fs_object_ptr->fs_object_map);
        free(fs_object_ptr);

        long size_of_dir = sizeof(fs_object) + sizeof(struct stat); // size required to store info about dir

        size_t old_size = fs_object_parent_ptr->fs_object_details->st_size;
        long updated_size = old_size - size_of_dir;
        if(updated_size < 0)
        	updated_size = 0;
        fs_object_parent_ptr->fs_object_details->st_size = updated_size;

        available_size = available_size + size_of_dir;
    }
    else
    {
        return -ENOENT;
    }
    printf("Exit ramdisk_rmdir\n");
    return 0;
}

/*
	# Opens the directory at given path
	# We do not need to actually open the directory, just return 0
*/
static int ramdisk_opendir(const char *path, struct fuse_file_info *fi)
{
    printf("ramdisk_opendir path : %s\n", path);
    int result = 0;
    fs_object *fs_object_ptr = get_fs_object(path);
    if (fs_object_ptr == NULL) 
    {
        result = -ENOENT;
    }
    printf("Exit ramdisk_opendir\n");
	return result;
}

/*
	# Reads the directory at given path
*/
static int ramdisk_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    printf("ramdisk_readdir path : %s\n", path);
    fs_object *fs_object_ptr = get_fs_object(path);
    if(fs_object_ptr != NULL)
    {
        filler(buf, ".", NULL, 0);
        filler(buf, "..", NULL, 0);
        fs_object *fs_object_tmp;
        int map_size = hashmap_length(fs_object_ptr->fs_object_map);
        char* keys[map_size];
        int numKeys = hashmap_keys(fs_object_ptr->fs_object_map, keys);
        int i = 0;
        for(i = 0; i < numKeys; i++)
        {
            filler(buf, keys[i], NULL, 0);
        }
    }
    else
    {
        return -ENOENT;
    }
    printf("Exit readdir\n");
    return 0;
}

/*
	# Reading the metadata of the given path.
	# Always called before the operation made on the filesystem.
*/
static int ramdisk_getattr(const char *path, struct stat *stbuf)
{
    printf("ramdisk_getattr path : %s\n", path);
    int result = 0;
    fs_object *fs_object_ptr = get_fs_object(path);
    if(fs_object_ptr != NULL)
    {
        stbuf->st_nlink = fs_object_ptr->fs_object_details->st_nlink;
        stbuf->st_mode = fs_object_ptr->fs_object_details->st_mode;
        stbuf->st_size = fs_object_ptr->fs_object_details->st_size;
        stbuf->st_mtime = fs_object_ptr->fs_object_details->st_mtime;
        stbuf->st_ctime = fs_object_ptr->fs_object_details->st_ctime;
        
        result = 0;
    }
    else
    {
        result = -ENOENT;
    }
    printf("Exit getattr\n");
    return result;
}

/*
	# Closes the file at given path
	# We do not need to actually close the file, just returning 0 is sufficient
*/
static int ramdisk_release(const char *path, struct fuse_file_info *fi)
{
	printf("ramdisk_release path : %s\n", path);
    int result = 0;
    fs_object *fs_object_ptr = get_fs_object(path);
    if (fs_object_ptr == NULL) 
    {
        result = -ENOENT;
    }
    printf("Exit ramdisk_release\n");
    return result;
}

/*
	# Function to handle time setting warning when file is created using touch command
	# warning in touch abc.txt
	# We do not need to actually implement the function, just return 0
*/
static int ramdisk_utime(const char *path, struct utimbuf *ubuf)
{
	printf("ramdisk_utime path : %s\n", path);
    int result = 0;
    fs_object *fs_object_ptr = get_fs_object(path);
    if (fs_object_ptr == NULL) 
    {
        result = -ENOENT;
    }
    printf("Exit ramdisk_utime\n");
    return result;
}

/*
    # Function to handle error occured in echo 
    # while writing data to already existing file 
    # handling error in [echo "new data" > abc.txt] (abc.txt is an existing file)
    # We do not need to implement actual function
    # just returning 0 is sufficient
*/
static int ramdisk_truncate(const char *path, off_t offset)
{
    printf("ramdisk_truncate path : %s\n", path);
    int result = 0;
    fs_object *fs_object_ptr = get_fs_object(path);
    if (fs_object_ptr == NULL) 
    {
        result = -ENOENT;
    }
    printf("Exit ramdisk_truncate\n");
    return result;
}

static struct fuse_operations ramdisk_operations =
{
    .open = ramdisk_open,
    .release = ramdisk_release,
    .read = ramdisk_read,
    .write = ramdisk_write,
    .create = ramdisk_create,
    .mkdir = ramdisk_mkdir,
    .unlink = ramdisk_unlink,
    .rmdir = ramdisk_rmdir,
    .opendir = ramdisk_opendir,
    .readdir = ramdisk_readdir,
    .getattr = ramdisk_getattr,
    .truncate = ramdisk_truncate,
    .utime = ramdisk_utime
};

/*
	# Initialize Root
*/
void intialize_root()
{
	root = (fs_object *)malloc(sizeof(fs_object));
	root->fs_object_details = (struct stat *)malloc(sizeof(struct stat));
	root->fs_object_name = malloc(FILENAME_SIZE * sizeof(char));
	strcpy(root->fs_object_name, "/");

	long size_of_dir = sizeof(fs_object) + sizeof(struct stat); // size required to store info about dir

    root->fs_object_details->st_mode = S_IFDIR | 0755;
	root->fs_object_details->st_nlink = 2;
	root->fs_object_details->st_size = size_of_dir;
    time_t current_time;
    time(&current_time);
	root->fs_object_details->st_mtime = current_time;
	root->fs_object_details->st_ctime = current_time;
	root->parent_directory = NULL;
	root->fs_object_map = hashmap_new();
	root->fs_object_content = NULL;
	root->fs_object_type = IS_DIRECTORY;

	// update available memory size
	available_size = available_size - size_of_dir;
}

/*
	# Main Function
*/
int main(int argc, char *argv[])
{
	if(argc < 3 || argc > 4)
	{
		printf("Invalid arguments : ramdisk [mount_path] [size] [filename(optional)]\n");
		exit(0);
	}
	if(argc == 3)
	{
		printf("Starting new filesystem.\n");
	}
	if(argc == 4)
	{
		load_fs = 1;
		fs_path = argv[3];
		printf("Loading filesystem from %s\n", fs_path);
		argv[3] = NULL;
		argc--;
	}
	fs_size = atol(argv[2]) * 1024 * 1024; //converting to MB
	available_size = fs_size;
	if(fs_size < 1)
	{
		printf("Filesystem size must be greater than zero.\n");
		exit(0);
	}
	argv[2] = NULL;
	argc--;

	// initializing root directory
	intialize_root();

	// mounting the given location
	return fuse_main(argc, argv, &ramdisk_operations, NULL);
}