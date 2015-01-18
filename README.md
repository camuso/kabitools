kabiparser
==========

Parse .i files for symbols that must be kabi protected

Parse .i files for symbols that must be kabi protected

Preparation

1. Save makei.sh in redhat/scripts
   From the top of the kernel tree ...
      $ makei.sh ./

   This takes somewhat longer than building the kernel.
   Ultimately this should be done as a make target.

2. git clone git@github.com:camuso/kabiparser.git

   $ make kabi
   $ cp kabi redhat/kabi/kabi-parser

3. Copy kabilist.sh script to redhat/kabi/.
   From the top of the kernel tree run ...

   $ redhat/kabi/kabilist.sh

   This takes a just a few minutes and creates
   redhat/kabi/kabilist.log

To see which exported functions use a given data structure

Copy attached kabilist-search.sh to redhat/kabi/.

From the top of the kernel tree, invoke kabilist-search with
any regex.

$ kabilist-search "struct bio_set"
struct bio_set *fs_bio_set IN struct bio_set *fs_bio_set
struct bio_set *bs IN function struct bio *bio_alloc_bioset
struct bio_set *bs IN function struct bio *bio_clone_bioset
struct bio_set *bs IN function void bioset_free
function struct bio_set *bioset_create IN function struct bio_set *bioset_create
struct bio_set *bs IN function int bioset_integrity_create
struct bio_set *bs IN function void bioset_integrity_free

