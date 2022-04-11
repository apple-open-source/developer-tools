#include "clar_libgit2.h"
#include "git2/odb_backend.h"

#include "futils.h"
#include "odb.h"

typedef struct object_data {
    char *id;     /* object id (sha1)                          */
    char *dir;    /* object store (fan-out) directory name     */
    char *file;   /* object store filename                     */
} object_data;

static const char *odb_dir = "test-objects";

void test_body(object_data *d, git_rawobj *o);



/* Helpers */
static void remove_object_files(object_data *d)
{
   cl_git_pass(p_unlink(d->file));
   cl_git_pass(p_rmdir(d->dir));
   cl_assert(errno != ENOTEMPTY);
   cl_git_pass(p_rmdir(odb_dir) < 0);
}

static void streaming_write(git_oid *oid, git_odb *odb, git_rawobj *raw)
{
   git_odb_stream *stream;
   int error;

   cl_git_pass(git_odb_open_wstream(&stream, odb, raw->len, raw->type));
   git_odb_stream_write(stream, raw->data, raw->len);
   error = git_odb_stream_finalize_write(oid, stream);
   git_odb_stream_free(stream);
   cl_git_pass(error);
}

static void check_object_files(object_data *d)
{
   cl_assert(git_path_exists(d->dir));
   cl_assert(git_path_exists(d->file));
}

static void cmp_objects(git_rawobj *o1, git_rawobj *o2)
{
   cl_assert(o1->type == o2->type);
   cl_assert(o1->len == o2->len);
   if (o1->len > 0)
      cl_assert(memcmp(o1->data, o2->data, o1->len) == 0);
}

static void make_odb_dir(void)
{
	cl_git_pass(p_mkdir(odb_dir, GIT_OBJECT_DIR_MODE));
}


/* Standard test form */
void test_body(object_data *d, git_rawobj *o)
{
   git_odb *db;
   git_oid id1, id2;
   git_odb_object *obj;
   git_rawobj tmp;

   make_odb_dir();
   cl_git_pass(git_odb_open(&db, odb_dir));
   cl_git_pass(git_oid_fromstr(&id1, d->id));

   streaming_write(&id2, db, o);
   cl_assert(git_oid_cmp(&id1, &id2) == 0);
   check_object_files(d);

   cl_git_pass(git_odb_read(&obj, db, &id1));

   tmp.data = obj->buffer;
   tmp.len = obj->cached.size;
   tmp.type = obj->cached.type;

   cmp_objects(&tmp, o);

   git_odb_object_free(obj);
   git_odb_free(db);
   remove_object_files(d);
}


void test_object_raw_write__loose_object(void)
{
   object_data commit = {
      "3d7f8a6af076c8c3f20071a8935cdbe8228594d1",
      "test-objects/3d",
      "test-objects/3d/7f8a6af076c8c3f20071a8935cdbe8228594d1",
   };

   unsigned char commit_data[] = {
      0x74, 0x72, 0x65, 0x65, 0x20, 0x64, 0x66, 0x66,
      0x32, 0x64, 0x61, 0x39, 0x30, 0x62, 0x32, 0x35,
      0x34, 0x65, 0x31, 0x62, 0x65, 0x62, 0x38, 0x38,
      0x39, 0x64, 0x31, 0x66, 0x31, 0x66, 0x31, 0x32,
      0x38, 0x38, 0x62, 0x65, 0x31, 0x38, 0x30, 0x33,
      0x37, 0x38, 0x32, 0x64, 0x66, 0x0a, 0x61, 0x75,
      0x74, 0x68, 0x6f, 0x72, 0x20, 0x41, 0x20, 0x55,
      0x20, 0x54, 0x68, 0x6f, 0x72, 0x20, 0x3c, 0x61,
      0x75, 0x74, 0x68, 0x6f, 0x72, 0x40, 0x65, 0x78,
      0x61, 0x6d, 0x70, 0x6c, 0x65, 0x2e, 0x63, 0x6f,
      0x6d, 0x3e, 0x20, 0x31, 0x32, 0x32, 0x37, 0x38,
      0x31, 0x34, 0x32, 0x39, 0x37, 0x20, 0x2b, 0x30,
      0x30, 0x30, 0x30, 0x0a, 0x63, 0x6f, 0x6d, 0x6d,
      0x69, 0x74, 0x74, 0x65, 0x72, 0x20, 0x43, 0x20,
      0x4f, 0x20, 0x4d, 0x69, 0x74, 0x74, 0x65, 0x72,
      0x20, 0x3c, 0x63, 0x6f, 0x6d, 0x6d, 0x69, 0x74,
      0x74, 0x65, 0x72, 0x40, 0x65, 0x78, 0x61, 0x6d,
      0x70, 0x6c, 0x65, 0x2e, 0x63, 0x6f, 0x6d, 0x3e,
      0x20, 0x31, 0x32, 0x32, 0x37, 0x38, 0x31, 0x34,
      0x32, 0x39, 0x37, 0x20, 0x2b, 0x30, 0x30, 0x30,
      0x30, 0x0a, 0x0a, 0x41, 0x20, 0x6f, 0x6e, 0x65,
      0x2d, 0x6c, 0x69, 0x6e, 0x65, 0x20, 0x63, 0x6f,
      0x6d, 0x6d, 0x69, 0x74, 0x20, 0x73, 0x75, 0x6d,
      0x6d, 0x61, 0x72, 0x79, 0x0a, 0x0a, 0x54, 0x68,
      0x65, 0x20, 0x62, 0x6f, 0x64, 0x79, 0x20, 0x6f,
      0x66, 0x20, 0x74, 0x68, 0x65, 0x20, 0x63, 0x6f,
      0x6d, 0x6d, 0x69, 0x74, 0x20, 0x6d, 0x65, 0x73,
      0x73, 0x61, 0x67, 0x65, 0x2c, 0x20, 0x63, 0x6f,
      0x6e, 0x74, 0x61, 0x69, 0x6e, 0x69, 0x6e, 0x67,
      0x20, 0x66, 0x75, 0x72, 0x74, 0x68, 0x65, 0x72,
      0x20, 0x65, 0x78, 0x70, 0x6c, 0x61, 0x6e, 0x61,
      0x74, 0x69, 0x6f, 0x6e, 0x0a, 0x6f, 0x66, 0x20,
      0x74, 0x68, 0x65, 0x20, 0x70, 0x75, 0x72, 0x70,
      0x6f, 0x73, 0x65, 0x20, 0x6f, 0x66, 0x20, 0x74,
      0x68, 0x65, 0x20, 0x63, 0x68, 0x61, 0x6e, 0x67,
      0x65, 0x73, 0x20, 0x69, 0x6e, 0x74, 0x72, 0x6f,
      0x64, 0x75, 0x63, 0x65, 0x64, 0x20, 0x62, 0x79,
      0x20, 0x74, 0x68, 0x65, 0x20, 0x63, 0x6f, 0x6d,
      0x6d, 0x69, 0x74, 0x2e, 0x0a, 0x0a, 0x53, 0x69,
      0x67, 0x6e, 0x65, 0x64, 0x2d, 0x6f, 0x66, 0x2d,
      0x62, 0x79, 0x3a, 0x20, 0x41, 0x20, 0x55, 0x20,
      0x54, 0x68, 0x6f, 0x72, 0x20, 0x3c, 0x61, 0x75,
      0x74, 0x68, 0x6f, 0x72, 0x40, 0x65, 0x78, 0x61,
      0x6d, 0x70, 0x6c, 0x65, 0x2e, 0x63, 0x6f, 0x6d,
      0x3e, 0x0a,
   };

   git_rawobj commit_obj = {
      commit_data,
      sizeof(commit_data),
      GIT_OBJECT_COMMIT
   };

   test_body(&commit, &commit_obj);
}

void test_object_raw_write__loose_tree(void)
{
   static object_data tree = {
      "dff2da90b254e1beb889d1f1f1288be1803782df",
      "test-objects/df",
      "test-objects/df/f2da90b254e1beb889d1f1f1288be1803782df",
   };

   static unsigned char tree_data[] = {
      0x31, 0x30, 0x30, 0x36, 0x34, 0x34, 0x20, 0x6f,
      0x6e, 0x65, 0x00, 0x8b, 0x13, 0x78, 0x91, 0x79,
      0x1f, 0xe9, 0x69, 0x27, 0xad, 0x78, 0xe6, 0x4b,
      0x0a, 0xad, 0x7b, 0xde, 0xd0, 0x8b, 0xdc, 0x31,
      0x30, 0x30, 0x36, 0x34, 0x34, 0x20, 0x73, 0x6f,
      0x6d, 0x65, 0x00, 0xfd, 0x84, 0x30, 0xbc, 0x86,
      0x4c, 0xfc, 0xd5, 0xf1, 0x0e, 0x55, 0x90, 0xf8,
      0xa4, 0x47, 0xe0, 0x1b, 0x94, 0x2b, 0xfe, 0x31,
      0x30, 0x30, 0x36, 0x34, 0x34, 0x20, 0x74, 0x77,
      0x6f, 0x00, 0x78, 0x98, 0x19, 0x22, 0x61, 0x3b,
      0x2a, 0xfb, 0x60, 0x25, 0x04, 0x2f, 0xf6, 0xbd,
      0x87, 0x8a, 0xc1, 0x99, 0x4e, 0x85, 0x31, 0x30,
      0x30, 0x36, 0x34, 0x34, 0x20, 0x7a, 0x65, 0x72,
      0x6f, 0x00, 0xe6, 0x9d, 0xe2, 0x9b, 0xb2, 0xd1,
      0xd6, 0x43, 0x4b, 0x8b, 0x29, 0xae, 0x77, 0x5a,
      0xd8, 0xc2, 0xe4, 0x8c, 0x53, 0x91,
   };

   static git_rawobj tree_obj = {
      tree_data,
      sizeof(tree_data),
      GIT_OBJECT_TREE
   };

   test_body(&tree, &tree_obj);
}

void test_object_raw_write__loose_tag(void)
{
   static object_data tag = {
      "09d373e1dfdc16b129ceec6dd649739911541e05",
      "test-objects/09",
      "test-objects/09/d373e1dfdc16b129ceec6dd649739911541e05",
   };

   static unsigned char tag_data[] = {
      0x6f, 0x62, 0x6a, 0x65, 0x63, 0x74, 0x20, 0x33,
      0x64, 0x37, 0x66, 0x38, 0x61, 0x36, 0x61, 0x66,
      0x30, 0x37, 0x36, 0x63, 0x38, 0x63, 0x33, 0x66,
      0x32, 0x30, 0x30, 0x37, 0x31, 0x61, 0x38, 0x39,
      0x33, 0x35, 0x63, 0x64, 0x62, 0x65, 0x38, 0x32,
      0x32, 0x38, 0x35, 0x39, 0x34, 0x64, 0x31, 0x0a,
      0x74, 0x79, 0x70, 0x65, 0x20, 0x63, 0x6f, 0x6d,
      0x6d, 0x69, 0x74, 0x0a, 0x74, 0x61, 0x67, 0x20,
      0x76, 0x30, 0x2e, 0x30, 0x2e, 0x31, 0x0a, 0x74,
      0x61, 0x67, 0x67, 0x65, 0x72, 0x20, 0x43, 0x20,
      0x4f, 0x20, 0x4d, 0x69, 0x74, 0x74, 0x65, 0x72,
      0x20, 0x3c, 0x63, 0x6f, 0x6d, 0x6d, 0x69, 0x74,
      0x74, 0x65, 0x72, 0x40, 0x65, 0x78, 0x61, 0x6d,
      0x70, 0x6c, 0x65, 0x2e, 0x63, 0x6f, 0x6d, 0x3e,
      0x20, 0x31, 0x32, 0x32, 0x37, 0x38, 0x31, 0x34,
      0x32, 0x39, 0x37, 0x20, 0x2b, 0x30, 0x30, 0x30,
      0x30, 0x0a, 0x0a, 0x54, 0x68, 0x69, 0x73, 0x20,
      0x69, 0x73, 0x20, 0x74, 0x68, 0x65, 0x20, 0x74,
      0x61, 0x67, 0x20, 0x6f, 0x62, 0x6a, 0x65, 0x63,
      0x74, 0x20, 0x66, 0x6f, 0x72, 0x20, 0x72, 0x65,
      0x6c, 0x65, 0x61, 0x73, 0x65, 0x20, 0x76, 0x30,
      0x2e, 0x30, 0x2e, 0x31, 0x0a,
   };

   static git_rawobj tag_obj = {
      tag_data,
      sizeof(tag_data),
      GIT_OBJECT_TAG
   };


   test_body(&tag, &tag_obj);
}

void test_object_raw_write__zero_length(void)
{
   static object_data zero = {
      "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391",
      "test-objects/e6",
      "test-objects/e6/9de29bb2d1d6434b8b29ae775ad8c2e48c5391",
   };

   static unsigned char zero_data[] = {
      0x00  /* dummy data */
   };

   static git_rawobj zero_obj = {
      zero_data,
      0,
      GIT_OBJECT_BLOB
   };

   test_body(&zero, &zero_obj);
}

void test_object_raw_write__one_byte(void)
{
   static object_data one = {
      "8b137891791fe96927ad78e64b0aad7bded08bdc",
      "test-objects/8b",
      "test-objects/8b/137891791fe96927ad78e64b0aad7bded08bdc",
   };

   static unsigned char one_data[] = {
      0x0a,
   };

   static git_rawobj one_obj = {
      one_data,
      sizeof(one_data),
      GIT_OBJECT_BLOB
   };

   test_body(&one, &one_obj);
}

void test_object_raw_write__two_byte(void)
{
   static object_data two = {
      "78981922613b2afb6025042ff6bd878ac1994e85",
      "test-objects/78",
      "test-objects/78/981922613b2afb6025042ff6bd878ac1994e85",
   };

   static unsigned char two_data[] = {
      0x61, 0x0a,
   };

   static git_rawobj two_obj = {
      two_data,
      sizeof(two_data),
      GIT_OBJECT_BLOB
   };

   test_body(&two, &two_obj);
}

void test_object_raw_write__several_bytes(void)
{
   static object_data some = {
      "fd8430bc864cfcd5f10e5590f8a447e01b942bfe",
      "test-objects/fd",
      "test-objects/fd/8430bc864cfcd5f10e5590f8a447e01b942bfe",
   };

   static unsigned char some_data[] = {
      0x2f, 0x2a, 0x0a, 0x20, 0x2a, 0x20, 0x54, 0x68,
      0x69, 0x73, 0x20, 0x66, 0x69, 0x6c, 0x65, 0x20,
      0x69, 0x73, 0x20, 0x66, 0x72, 0x65, 0x65, 0x20,
      0x73, 0x6f, 0x66, 0x74, 0x77, 0x61, 0x72, 0x65,
      0x3b, 0x20, 0x79, 0x6f, 0x75, 0x20, 0x63, 0x61,
      0x6e, 0x20, 0x72, 0x65, 0x64, 0x69, 0x73, 0x74,
      0x72, 0x69, 0x62, 0x75, 0x74, 0x65, 0x20, 0x69,
      0x74, 0x20, 0x61, 0x6e, 0x64, 0x2f, 0x6f, 0x72,
      0x20, 0x6d, 0x6f, 0x64, 0x69, 0x66, 0x79, 0x0a,
      0x20, 0x2a, 0x20, 0x69, 0x74, 0x20, 0x75, 0x6e,
      0x64, 0x65, 0x72, 0x20, 0x74, 0x68, 0x65, 0x20,
      0x74, 0x65, 0x72, 0x6d, 0x73, 0x20, 0x6f, 0x66,
      0x20, 0x74, 0x68, 0x65, 0x20, 0x47, 0x4e, 0x55,
      0x20, 0x47, 0x65, 0x6e, 0x65, 0x72, 0x61, 0x6c,
      0x20, 0x50, 0x75, 0x62, 0x6c, 0x69, 0x63, 0x20,
      0x4c, 0x69, 0x63, 0x65, 0x6e, 0x73, 0x65, 0x2c,
      0x20, 0x76, 0x65, 0x72, 0x73, 0x69, 0x6f, 0x6e,
      0x20, 0x32, 0x2c, 0x0a, 0x20, 0x2a, 0x20, 0x61,
      0x73, 0x20, 0x70, 0x75, 0x62, 0x6c, 0x69, 0x73,
      0x68, 0x65, 0x64, 0x20, 0x62, 0x79, 0x20, 0x74,
      0x68, 0x65, 0x20, 0x46, 0x72, 0x65, 0x65, 0x20,
      0x53, 0x6f, 0x66, 0x74, 0x77, 0x61, 0x72, 0x65,
      0x20, 0x46, 0x6f, 0x75, 0x6e, 0x64, 0x61, 0x74,
      0x69, 0x6f, 0x6e, 0x2e, 0x0a, 0x20, 0x2a, 0x0a,
      0x20, 0x2a, 0x20, 0x49, 0x6e, 0x20, 0x61, 0x64,
      0x64, 0x69, 0x74, 0x69, 0x6f, 0x6e, 0x20, 0x74,
      0x6f, 0x20, 0x74, 0x68, 0x65, 0x20, 0x70, 0x65,
      0x72, 0x6d, 0x69, 0x73, 0x73, 0x69, 0x6f, 0x6e,
      0x73, 0x20, 0x69, 0x6e, 0x20, 0x74, 0x68, 0x65,
      0x20, 0x47, 0x4e, 0x55, 0x20, 0x47, 0x65, 0x6e,
      0x65, 0x72, 0x61, 0x6c, 0x20, 0x50, 0x75, 0x62,
      0x6c, 0x69, 0x63, 0x20, 0x4c, 0x69, 0x63, 0x65,
      0x6e, 0x73, 0x65, 0x2c, 0x0a, 0x20, 0x2a, 0x20,
      0x74, 0x68, 0x65, 0x20, 0x61, 0x75, 0x74, 0x68,
      0x6f, 0x72, 0x73, 0x20, 0x67, 0x69, 0x76, 0x65,
      0x20, 0x79, 0x6f, 0x75, 0x20, 0x75, 0x6e, 0x6c,
      0x69, 0x6d, 0x69, 0x74, 0x65, 0x64, 0x20, 0x70,
      0x65, 0x72, 0x6d, 0x69, 0x73, 0x73, 0x69, 0x6f,
      0x6e, 0x20, 0x74, 0x6f, 0x20, 0x6c, 0x69, 0x6e,
      0x6b, 0x20, 0x74, 0x68, 0x65, 0x20, 0x63, 0x6f,
      0x6d, 0x70, 0x69, 0x6c, 0x65, 0x64, 0x0a, 0x20,
      0x2a, 0x20, 0x76, 0x65, 0x72, 0x73, 0x69, 0x6f,
      0x6e, 0x20, 0x6f, 0x66, 0x20, 0x74, 0x68, 0x69,
      0x73, 0x20, 0x66, 0x69, 0x6c, 0x65, 0x20, 0x69,
      0x6e, 0x74, 0x6f, 0x20, 0x63, 0x6f, 0x6d, 0x62,
      0x69, 0x6e, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x73,
      0x20, 0x77, 0x69, 0x74, 0x68, 0x20, 0x6f, 0x74,
      0x68, 0x65, 0x72, 0x20, 0x70, 0x72, 0x6f, 0x67,
      0x72, 0x61, 0x6d, 0x73, 0x2c, 0x0a, 0x20, 0x2a,
      0x20, 0x61, 0x6e, 0x64, 0x20, 0x74, 0x6f, 0x20,
      0x64, 0x69, 0x73, 0x74, 0x72, 0x69, 0x62, 0x75,
      0x74, 0x65, 0x20, 0x74, 0x68, 0x6f, 0x73, 0x65,
      0x20, 0x63, 0x6f, 0x6d, 0x62, 0x69, 0x6e, 0x61,
      0x74, 0x69, 0x6f, 0x6e, 0x73, 0x20, 0x77, 0x69,
      0x74, 0x68, 0x6f, 0x75, 0x74, 0x20, 0x61, 0x6e,
      0x79, 0x20, 0x72, 0x65, 0x73, 0x74, 0x72, 0x69,
      0x63, 0x74, 0x69, 0x6f, 0x6e, 0x0a, 0x20, 0x2a,
      0x20, 0x63, 0x6f, 0x6d, 0x69, 0x6e, 0x67, 0x20,
      0x66, 0x72, 0x6f, 0x6d, 0x20, 0x74, 0x68, 0x65,
      0x20, 0x75, 0x73, 0x65, 0x20, 0x6f, 0x66, 0x20,
      0x74, 0x68, 0x69, 0x73, 0x20, 0x66, 0x69, 0x6c,
      0x65, 0x2e, 0x20, 0x20, 0x28, 0x54, 0x68, 0x65,
      0x20, 0x47, 0x65, 0x6e, 0x65, 0x72, 0x61, 0x6c,
      0x20, 0x50, 0x75, 0x62, 0x6c, 0x69, 0x63, 0x20,
      0x4c, 0x69, 0x63, 0x65, 0x6e, 0x73, 0x65, 0x0a,
      0x20, 0x2a, 0x20, 0x72, 0x65, 0x73, 0x74, 0x72,
      0x69, 0x63, 0x74, 0x69, 0x6f, 0x6e, 0x73, 0x20,
      0x64, 0x6f, 0x20, 0x61, 0x70, 0x70, 0x6c, 0x79,
      0x20, 0x69, 0x6e, 0x20, 0x6f, 0x74, 0x68, 0x65,
      0x72, 0x20, 0x72, 0x65, 0x73, 0x70, 0x65, 0x63,
      0x74, 0x73, 0x3b, 0x20, 0x66, 0x6f, 0x72, 0x20,
      0x65, 0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x2c,
      0x20, 0x74, 0x68, 0x65, 0x79, 0x20, 0x63, 0x6f,
      0x76, 0x65, 0x72, 0x0a, 0x20, 0x2a, 0x20, 0x6d,
      0x6f, 0x64, 0x69, 0x66, 0x69, 0x63, 0x61, 0x74,
      0x69, 0x6f, 0x6e, 0x20, 0x6f, 0x66, 0x20, 0x74,
      0x68, 0x65, 0x20, 0x66, 0x69, 0x6c, 0x65, 0x2c,
      0x20, 0x61, 0x6e, 0x64, 0x20, 0x64, 0x69, 0x73,
      0x74, 0x72, 0x69, 0x62, 0x75, 0x74, 0x69, 0x6f,
      0x6e, 0x20, 0x77, 0x68, 0x65, 0x6e, 0x20, 0x6e,
      0x6f, 0x74, 0x20, 0x6c, 0x69, 0x6e, 0x6b, 0x65,
      0x64, 0x20, 0x69, 0x6e, 0x74, 0x6f, 0x0a, 0x20,
      0x2a, 0x20, 0x61, 0x20, 0x63, 0x6f, 0x6d, 0x62,
      0x69, 0x6e, 0x65, 0x64, 0x20, 0x65, 0x78, 0x65,
      0x63, 0x75, 0x74, 0x61, 0x62, 0x6c, 0x65, 0x2e,
      0x29, 0x0a, 0x20, 0x2a, 0x0a, 0x20, 0x2a, 0x20,
      0x54, 0x68, 0x69, 0x73, 0x20, 0x66, 0x69, 0x6c,
      0x65, 0x20, 0x69, 0x73, 0x20, 0x64, 0x69, 0x73,
      0x74, 0x72, 0x69, 0x62, 0x75, 0x74, 0x65, 0x64,
      0x20, 0x69, 0x6e, 0x20, 0x74, 0x68, 0x65, 0x20,
      0x68, 0x6f, 0x70, 0x65, 0x20, 0x74, 0x68, 0x61,
      0x74, 0x20, 0x69, 0x74, 0x20, 0x77, 0x69, 0x6c,
      0x6c, 0x20, 0x62, 0x65, 0x20, 0x75, 0x73, 0x65,
      0x66, 0x75, 0x6c, 0x2c, 0x20, 0x62, 0x75, 0x74,
      0x0a, 0x20, 0x2a, 0x20, 0x57, 0x49, 0x54, 0x48,
      0x4f, 0x55, 0x54, 0x20, 0x41, 0x4e, 0x59, 0x20,
      0x57, 0x41, 0x52, 0x52, 0x41, 0x4e, 0x54, 0x59,
      0x3b, 0x20, 0x77, 0x69, 0x74, 0x68, 0x6f, 0x75,
      0x74, 0x20, 0x65, 0x76, 0x65, 0x6e, 0x20, 0x74,
      0x68, 0x65, 0x20, 0x69, 0x6d, 0x70, 0x6c, 0x69,
      0x65, 0x64, 0x20, 0x77, 0x61, 0x72, 0x72, 0x61,
      0x6e, 0x74, 0x79, 0x20, 0x6f, 0x66, 0x0a, 0x20,
      0x2a, 0x20, 0x4d, 0x45, 0x52, 0x43, 0x48, 0x41,
      0x4e, 0x54, 0x41, 0x42, 0x49, 0x4c, 0x49, 0x54,
      0x59, 0x20, 0x6f, 0x72, 0x20, 0x46, 0x49, 0x54,
      0x4e, 0x45, 0x53, 0x53, 0x20, 0x46, 0x4f, 0x52,
      0x20, 0x41, 0x20, 0x50, 0x41, 0x52, 0x54, 0x49,
      0x43, 0x55, 0x4c, 0x41, 0x52, 0x20, 0x50, 0x55,
      0x52, 0x50, 0x4f, 0x53, 0x45, 0x2e, 0x20, 0x20,
      0x53, 0x65, 0x65, 0x20, 0x74, 0x68, 0x65, 0x20,
      0x47, 0x4e, 0x55, 0x0a, 0x20, 0x2a, 0x20, 0x47,
      0x65, 0x6e, 0x65, 0x72, 0x61, 0x6c, 0x20, 0x50,
      0x75, 0x62, 0x6c, 0x69, 0x63, 0x20, 0x4c, 0x69,
      0x63, 0x65, 0x6e, 0x73, 0x65, 0x20, 0x66, 0x6f,
      0x72, 0x20, 0x6d, 0x6f, 0x72, 0x65, 0x20, 0x64,
      0x65, 0x74, 0x61, 0x69, 0x6c, 0x73, 0x2e, 0x0a,
      0x20, 0x2a, 0x0a, 0x20, 0x2a, 0x20, 0x59, 0x6f,
      0x75, 0x20, 0x73, 0x68, 0x6f, 0x75, 0x6c, 0x64,
      0x20, 0x68, 0x61, 0x76, 0x65, 0x20, 0x72, 0x65,
      0x63, 0x65, 0x69, 0x76, 0x65, 0x64, 0x20, 0x61,
      0x20, 0x63, 0x6f, 0x70, 0x79, 0x20, 0x6f, 0x66,
      0x20, 0x74, 0x68, 0x65, 0x20, 0x47, 0x4e, 0x55,
      0x20, 0x47, 0x65, 0x6e, 0x65, 0x72, 0x61, 0x6c,
      0x20, 0x50, 0x75, 0x62, 0x6c, 0x69, 0x63, 0x20,
      0x4c, 0x69, 0x63, 0x65, 0x6e, 0x73, 0x65, 0x0a,
      0x20, 0x2a, 0x20, 0x61, 0x6c, 0x6f, 0x6e, 0x67,
      0x20, 0x77, 0x69, 0x74, 0x68, 0x20, 0x74, 0x68,
      0x69, 0x73, 0x20, 0x70, 0x72, 0x6f, 0x67, 0x72,
      0x61, 0x6d, 0x3b, 0x20, 0x73, 0x65, 0x65, 0x20,
      0x74, 0x68, 0x65, 0x20, 0x66, 0x69, 0x6c, 0x65,
      0x20, 0x43, 0x4f, 0x50, 0x59, 0x49, 0x4e, 0x47,
      0x2e, 0x20, 0x20, 0x49, 0x66, 0x20, 0x6e, 0x6f,
      0x74, 0x2c, 0x20, 0x77, 0x72, 0x69, 0x74, 0x65,
      0x20, 0x74, 0x6f, 0x0a, 0x20, 0x2a, 0x20, 0x74,
      0x68, 0x65, 0x20, 0x46, 0x72, 0x65, 0x65, 0x20,
      0x53, 0x6f, 0x66, 0x74, 0x77, 0x61, 0x72, 0x65,
      0x20, 0x46, 0x6f, 0x75, 0x6e, 0x64, 0x61, 0x74,
      0x69, 0x6f, 0x6e, 0x2c, 0x20, 0x35, 0x31, 0x20,
      0x46, 0x72, 0x61, 0x6e, 0x6b, 0x6c, 0x69, 0x6e,
      0x20, 0x53, 0x74, 0x72, 0x65, 0x65, 0x74, 0x2c,
      0x20, 0x46, 0x69, 0x66, 0x74, 0x68, 0x20, 0x46,
      0x6c, 0x6f, 0x6f, 0x72, 0x2c, 0x0a, 0x20, 0x2a,
      0x20, 0x42, 0x6f, 0x73, 0x74, 0x6f, 0x6e, 0x2c,
      0x20, 0x4d, 0x41, 0x20, 0x30, 0x32, 0x31, 0x31,
      0x30, 0x2d, 0x31, 0x33, 0x30, 0x31, 0x2c, 0x20,
      0x55, 0x53, 0x41, 0x2e, 0x0a, 0x20, 0x2a, 0x2f,
      0x0a,
   };

   static git_rawobj some_obj = {
      some_data,
      sizeof(some_data),
      GIT_OBJECT_BLOB
   };

   test_body(&some, &some_obj);
}
