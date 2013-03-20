#pragma once
// Blender 2.56 SDNA

typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

// Header for a file-block (BHead in blender)
struct BlockHeader {
    char identifier[4];
    uint32_t size; // Total length of the data after the block header
    uint64_t address; // Base memory address used by pointers pointing in this block
    uint32_t type; // Type of the stored structure (as an index in SDNA types)
    uint32_t count; // Number of structures located in this block
};

struct ID {
 ID* next;
 ID* prev;
 ID* newid;
 struct Library* lib;
 char name[66];
 short pad;
 short us;
 short flag;
 int icon_id;
 int pad2;
 struct IDProperty* properties;
};

template<class T> struct ID_iterator {
    const ID* pointer;
    ID_iterator(const ID* pointer):pointer(pointer){}
    void operator++() { pointer = pointer->next; }
    bool operator!=(const ID_iterator<T>& b) const { return pointer != b.pointer; }
    const T& operator*() const { return (const T&)*pointer; }
};

template<class T=void> struct ListBase {
 const ID* first;
 const ID* last;
 const ID_iterator<T> begin() const { return first; }
 const ID_iterator<T> end() const { return 0; }
};

struct bAnimVizSettings {
 int ghost_sf;
 int ghost_ef;
 int ghost_bc;
 int ghost_ac;
 short ghost_type;
 short ghost_step;
 short ghost_flag;
 short recalc;
 short path_type;
 short path_step;
 short path_viewflag;
 short path_bakeflag;
 int path_sf;
 int path_ef;
 int path_bc;
 int path_ac;
};

struct VolumeSettings {
 float density;
 float emission;
 float scattering;
 float reflection;
 float emission_col[3];
 float transmission_col[3];
 float reflection_col[3];
 float density_scale;
 float depth_cutoff;
 float asymmetry;
 short stepsizeType;
 short shadeflag;
 short shadeType;
 short precache_resolution;
 float stepsize;
 float ms_diff;
 float ms_intensity;
 float ms_spread;
};

struct GameSettings {
 int flag;
 int alpha_blend;
 int face_orientation;
 int pad1;
};

struct Object;
struct Group {
 ID id;
 ListBase<Object> gobject;
 int layer;
 float dupli_ofs[3];
};

struct Material {
 ID id;
 struct AnimData* adt;
 short material_type;
 short flag;
 float r;
 float g;
 float b;
 float specr;
 float specg;
 float specb;
 float mirr;
 float mirg;
 float mirb;
 float ambr;
 float ambb;
 float ambg;
 float amb;
 float emit;
 float ang;
 float spectra;
 float ray_mirror;
 float alpha;
 float ref;
 float spec;
 float zoffs;
 float add;
 float translucency;
 VolumeSettings vol;
 GameSettings game;
 float fresnel_mir;
 float fresnel_mir_i;
 float fresnel_tra;
 float fresnel_tra_i;
 float filter;
 float tx_limit;
 float tx_falloff;
 short ray_depth;
 short ray_depth_tra;
 short har;
 char seed1;
 char seed2;
 float gloss_mir;
 float gloss_tra;
 short samp_gloss_mir;
 short samp_gloss_tra;
 float adapt_thresh_mir;
 float adapt_thresh_tra;
 float aniso_gloss_mir;
 float dist_mir;
 short fadeto_mir;
 short shade_flag;
 int mode;
 int mode_l;
 short flarec;
 short starc;
 short linec;
 short ringc;
 float hasize;
 float flaresize;
 float subsize;
 float flareboost;
 float strand_sta;
 float strand_end;
 float strand_ease;
 float strand_surfnor;
 float strand_min;
 float strand_widthfade;
 char strand_uvname[64];
 float sbias;
 float lbias;
 float shad_alpha;
 int septex;
 char rgbsel;
 char texact;
 char pr_type;
 char use_nodes;
 short pr_back;
 short pr_lamp;
 short pr_texture;
 short ml_flag;
 short diff_shader;
 short spec_shader;
 float roughness;
 float refrac;
 float param[4];
 float rms;
 float darkness;
 short texco;
 short mapto;
 struct ColorBand* ramp_col;
 struct ColorBand* ramp_spec;
 char rampin_col;
 char rampin_spec;
 char rampblend_col;
 char rampblend_spec;
 short ramp_show;
 short pad3;
 float rampfac_col;
 float rampfac_spec;
 struct MTex* mtex[18];
 struct bNodeTree* nodetree;
 struct Ipo* ipo;
 Group* group;
 struct PreviewImage* preview;
 float friction;
 float fh;
 float reflect;
 float fhdist;
 float xyfrict;
 short dynamode;
 short pad2;
 float sss_radius[3];
 float sss_col[3];
 float sss_error;
 float sss_scale;
 float sss_ior;
 float sss_colfac;
 float sss_texfac;
 float sss_front;
 float sss_back;
 short sss_flag;
 short sss_preset;
 int mapto_textured;
 short shadowonly_flag;
 short index;
 ListBase<> gpumaterial;
};

struct MPoly {
 int loopstart;
 int totloop;
 short mat_nr;
 char flag;
 char pad;
};

struct MLoop {
 int v;
 int e;
};

struct MLoopUV {
 float uv[2];
 int flag;
};

struct MVert {
 float co[3];
 short no[3];
 char flag;
 char bweight;
};

struct CustomData {
 struct CustomDataLayer* layers;
 int typemap[34];
 int totlayer;
 int maxlayer;
 int totsize;
 int pad2;
 void* pool;
 struct CustomDataExternal* external;
};

struct MDeformWeight {
 int def_nr;
 float weight;
};

struct MDeformVert {
 MDeformWeight* dw;
 int totweight;
 int flag;
};

struct Mesh {
 ID id;
 struct AnimData* adt;
 struct BoundBox* bb;
 struct Ipo* ipo;
 struct Key {} * key;
 Material** mat;
 MPoly* mpoly;
 struct MTexPoly* mtpoly;
 MLoop* mloop;
 MLoopUV* mloopuv;
 struct MLoopCol* mloopcol;
 struct MFace* mface;
 struct MTFace* mtface;
 struct TFace* tface;
 MVert* mvert;
 struct MEdge* medge;
 MDeformVert* dvert;
 struct MCol* mcol;
 struct MSticky* msticky;
 struct Mesh* texcomesh;
 struct MSelect* mselect;
 struct BMEditMesh* edit_btmesh;
 CustomData vdata;
 CustomData edata;
 CustomData fdata;
 CustomData pdata;
 CustomData ldata;
 int totvert;
 int totedge;
 int totface;
 int totselect;
 int totpoly;
 int totloop;
 int act_face;
 float loc[3];
 float size[3];
 float rot[3];
 short texflag;
 short drawflag;
 short smoothresh;
 short flag;
 short subdiv;
 short subdivr;
 char subsurftype;
 char editflag;
 short totcol;
 struct Multires* mr;
};

struct ParticleDupliWeight {
 ParticleDupliWeight* next;
 ParticleDupliWeight* prev;
 Object* ob;
 short count;
 short flag;
 short index;
 short rt;
};

struct ParticleSettings {
    ID id;
    struct AnimData* adt;
    struct BoidSettings* boids;
    struct SPHFluidSettings* fluid;
    struct EffectorWeights* effector_weights;
    int flag;
    int rt;
    short type;
    short from;
    short distr;
    short texact;
    enum { PART_PHYS_NEWTON=1 }; short phystype;
    short rotmode;
    short avemode;
    short reactevent;
    enum { PART_DRAW_EMITTER=8 }; short draw;
    short draw_as;
    short draw_size;
    short childtype;
    short ren_as;
    short subframes;
    short draw_col;
    short draw_step;
    short ren_step;
    short hair_step;
    short keys_step;
    short adapt_angle;
    short adapt_pix;
    short disp;
    short omat;
    short interpolation;
    short rotfrom;
    short integrator;
    short kink;
    short kink_axis;
    short bb_align;
    short bb_uv_split;
    short bb_anim;
    short bb_split_offset;
    float bb_tilt;
    float bb_rand_tilt;
    float bb_offset[2];
    float bb_size[2];
    float bb_vel_head;
    float bb_vel_tail;
    float color_vec_max;
    short simplify_flag;
    short simplify_refsize;
    float simplify_rate;
    float simplify_transition;
    float simplify_viewport;
    float sta;
    float end;
    float lifetime;
    float randlife;
    float timetweak;
    float courant_target;
    float jitfac;
    float eff_hair;
    float grid_rand;
    float ps_offset;
    int totpart;
    int userjit;
    int grid_res;
    int effector_amount;
    short time_flag;
    short time_pad[3];
    float normfac;
    float obfac;
    float randfac;
    float partfac;
    float tanfac;
    float tanphase;
    float reactfac;
    float ob_vel[3];
    float avefac;
    float phasefac;
    float randrotfac;
    float randphasefac;
    float mass;
    float size;
    float randsize;
    float acc[3];
    float dragfac;
    float brownfac;
    float dampfac;
    float randlength;
    int child_nbr;
    int ren_child_nbr;
    float parents;
    float childsize;
    float childrandsize;
    float childrad;
    float childflat;
    float clumpfac;
    float clumppow;
    float kink_amp;
    float kink_freq;
    float kink_shape;
    float kink_flat;
    float kink_amp_clump;
    float rough1;
    float rough1_size;
    float rough2;
    float rough2_size;
    float rough2_thres;
    float rough_end;
    float rough_end_shape;
    float clength;
    float clength_thres;
    float parting_fac;
    float parting_min;
    float parting_max;
    float branch_thres;
    float draw_line[2];
    float path_start;
    float path_end;
    int trail_count;
    int keyed_loops;
    struct MTex* mtex[18];
    Group* dup_group;
    ListBase<ParticleDupliWeight> dupliweights;
    Group* eff_group;
    Object* dup_ob;
    Object* bb_ob;
    struct Ipo* ipo;
    struct PartDeflect* pd;
    struct PartDeflect* pd2;
};

struct ParticleSystem {
    ParticleSystem* next;
    ParticleSystem* prev;
    ParticleSettings* part;
    struct ParticleData* particles;
    struct ChildParticle* child;
    struct PTCacheEdit* edit;
    void* free_edit;
    struct ParticleCacheKey** pathcache;
    struct ParticleCacheKey** childcache;
    ListBase<> pathcachebufs;
    ListBase<> childcachebufs;
    struct ClothModifierData* clmd;
    struct DerivedMesh* hair_in_dm;
    struct DerivedMesh* hair_out_dm;
    Object* target_ob;
    Object* lattice;
    Object* parent;
    ListBase<> targets;
    char name[64];
    float imat[16];
    float cfra;
    float tree_frame;
    float bvhtree_frame;
    int seed;
    int child_seed;
    int flag;
    int totpart;
    int totunexist;
    int totchild;
    int totcached;
    int totchildcache;
    short recalc;
    short target_psys;
    short totkeyed;
    short bakespace;
    char bb_uvname[192];
    enum { PSYS_VG_DENSITY=0 }; short vgroup[12];
    short vg_neg;
    short rt3;
    void* renderdata;
    struct PointCache* pointcache;
    ListBase<> ptcaches;
    ListBase<>* effectors;
    struct ParticleSpring* fluid_springs;
    int tot_fluidsprings;
    int alloc_fluidsprings;
    struct KDTree* tree;
    struct BVHTree* bvhtree;
    struct ParticleDrawData* pdd;
    float* frand;
    float dt_frac;
    float _pad;
};

struct Object {
 ID id;
 struct AnimData* adt;
 struct SculptSession* sculpt;
 enum { Empty, Mesh, Curve, Surf, Font, MBall, Lamp=10, Camera }; short type;
 short partype;
 int par1;
 int par2;
 int par3;
 char parsubstr[64];
 Object* parent;
 Object* track;
 Object* proxy;
 Object* proxy_group;
 Object* proxy_from;
 struct Ipo* ipo;
 struct BoundBox* bb;
 struct bAction* action;
 struct bAction* poselib;
 struct bPose* pose;
 struct Mesh* data;
 struct bGPdata* gpd;
 struct bAnimVizSettings avs;
 struct bMotionPath* mpath;
 ListBase<> constraintChannels;
 ListBase<> effect;
 ListBase<> disp;
 ListBase<> defbase;
 ListBase<> modifiers;
 int mode;
 int restore_mode;
 Material** mat;
 char* matbits;
 int totcol;
 int actcol;
 float loc[3];
 float dloc[3];
 float orig[3];
 float size[3];
 float dsize[3];
 float dscale[3];
 float rot[3];
 float drot[3];
 float quat[4];
 float dquat[4];
 float rotAxis[3];
 float drotAxis[3];
 float rotAngle;
 float drotAngle;
 float obmat[16];
 float parentinv[16];
 float constinv[16];
 float imat[16];
 float imat_ren[16];
 int lay;
 int pad6;
 short flag;
 short colbits;
 short transflag;
 short protectflag;
 short trackflag;
 short upflag;
 short nlaflag;
 short ipoflag;
 short scaflag;
 char scavisflag;
 char pad5;
 int dupon;
 int dupoff;
 int dupsta;
 int dupend;
 float sf;
 float ctime;
 float mass;
 float damping;
 float inertia;
 float formfactor;
 float rdamping;
 float sizefac;
 float margin;
 float max_vel;
 float min_vel;
 float m_contactProcessingThreshold;
 float obstacleRad;
 short rotmode;
 char boundtype;
 char collision_boundtype;
 char restrictflag;
 char dt;
 char dtx;
 char empty_drawtype;
 float empty_drawsize;
 float dupfacesca;
 ListBase<> prop;
 ListBase<> sensors;
 ListBase<> controllers;
 ListBase<> actuators;
 float bbsize[3];
 short index;
 short actdef;
 float col[4];
 int gameflag;
 int gameflag2;
 struct BulletSoftBody* bsoft;
 short softflag;
 short recalc;
 float anisotropicFriction[3];
 ListBase<> constraints;
 ListBase<> nlastrips;
 ListBase<> hooks;
 ListBase<ParticleSystem> particlesystem;
 struct PartDeflect* pd;
 struct SoftBody* soft;
 Group* dup_group;
 char body_type;
 char shapeflag;
 short shapenr;
 float smoothresh;
 struct FluidsimSettings* fluidsimSettings;
 struct DerivedMesh* derivedDeform;
 struct DerivedMesh* derivedFinal;
 uint64_t lastDataMask;
 uint64_t customdata_mask;
 int state;
 int init_state;
 ListBase<> gpulamp;
 ListBase<> pc_ids;
 ListBase<>* duplilist;
 float ima_ofs[2];
};

struct Base {
 Base* next;
 Base* prev;
 int lay;
 int selcol;
 int flag;
 short sx;
 short sy;
 Object* object;
};

struct ImageFormatData {
 char imtype;
 char depth;
 char planes;
 char flag;
 char quality;
 char compress;
 char exr_codec;
 char cineon_flag;
 short cineon_white;
 short cineon_black;
 float cineon_gamma;
 char jp2_flag;
 char pad[7];
};

struct QuicktimeCodecSettings {
 int codecType;
 int codecSpatialQuality;
 int codec;
 int codecFlags;
 int colorDepth;
 int codecTemporalQuality;
 int minSpatialQuality;
 int minTemporalQuality;
 int keyFrameRate;
 int bitRate;
 int audiocodecType;
 int audioSampleRate;
 short audioBitDepth;
 short audioChannels;
 int audioCodecFlags;
 int audioBitRate;
 int pad1;
};

struct FFMpegCodecData {
 int type;
 int codec;
 int audio_codec;
 int video_bitrate;
 int audio_bitrate;
 int audio_mixrate;
 int audio_channels;
 int audio_pad;
 float audio_volume;
 int gop_size;
 int flags;
 int rc_min_rate;
 int rc_max_rate;
 int rc_buffer_size;
 int mux_packet_size;
 int mux_rate;
 struct IDProperty* properties;
};

struct rctf {
 float xmin;
 float xmax;
 float ymin;
 float ymax;
};

struct rcti {
 int xmin;
 int xmax;
 int ymin;
 int ymax;
};

struct RenderData {
 ImageFormatData im_format;
 struct AviCodecData* avicodecdata;
 struct QuicktimeCodecData* qtcodecdata;
 QuicktimeCodecSettings qtcodecsettings;
 FFMpegCodecData ffcodecdata;
 int cfra;
 int sfra;
 int efra;
 float subframe;
 int psfra;
 int pefra;
 int images;
 int framapto;
 short flag;
 short threads;
 float framelen;
 float blurfac;
 float edgeR;
 float edgeG;
 float edgeB;
 short fullscreen;
 short xplay;
 short yplay;
 short freqplay;
 short depth;
 short attrib;
 int frame_step;
 short stereomode;
 short dimensionspreset;
 short filtertype;
 short size;
 short maximsize;
 short xsch;
 short ysch;
 short xparts;
 short yparts;
 short planes;
 short imtype;
 short subimtype;
 short quality;
 short displaymode;
 int scemode;
 int mode;
 int raytrace_options;
 short raytrace_structure;
 short pad1;
 short ocres;
 short pad4;
 short alphamode;
 short osa;
 short frs_sec;
 short edgeint;
 rctf safety;
 rctf border;
 rcti disprect;
 ListBase<> layers;
 short actlay;
 short mblur_samples;
 float xasp;
 float yasp;
 float frs_sec_base;
 float gauss;
 int color_mgt_flag;
 float postgamma;
 float posthue;
 float postsat;
 float dither_intensity;
 short bake_osa;
 short bake_filter;
 short bake_mode;
 short bake_flag;
 short bake_normal_space;
 short bake_quad_split;
 float bake_maxdist;
 float bake_biasdist;
 float bake_pad;
 char pic[1024];
 int stamp;
 short stamp_font_id;
 short pad3;
 char stamp_udata[768];
 float fg_stamp[4];
 float bg_stamp[4];
 char seq_prev_type;
 char seq_rend_type;
 char seq_flag;
 char pad5[5];
 int simplify_flag;
 short simplify_subsurf;
 short simplify_shadowsamples;
 float simplify_particles;
 float simplify_aosss;
 short cineonwhite;
 short cineonblack;
 float cineongamma;
 short jp2_preset;
 short jp2_depth;
 int rpad3;
 short domeres;
 short domemode;
 short domeangle;
 short dometilt;
 float domeresbuf;
 float pad2;
 struct Text* dometext;
 char engine[32];
};

struct AudioData {
 int mixrate;
 float main;
 float speed_of_sound;
 float doppler_factor;
 int distance_model;
 short flag;
 short pad;
 float volume;
 float pad2;
};

struct GameFraming {
 float col[3];
 char type;
 char pad1;
 char pad2;
 char pad3;
};

struct GameDome {
 short res;
 short mode;
 short angle;
 short tilt;
 float resbuf;
 float pad2;
 struct Text* warptext;
};

struct RecastData {
 float cellsize;
 float cellheight;
 float agentmaxslope;
 float agentmaxclimb;
 float agentheight;
 float agentradius;
 float edgemaxlen;
 float edgemaxerror;
 float regionminsize;
 float regionmergesize;
 int vertsperpoly;
 float detailsampledist;
 float detailsamplemaxerror;
 short pad1;
 short pad2;
};

struct GameData {
 GameFraming framing;
 short playerflag;
 short xplay;
 short yplay;
 short freqplay;
 short depth;
 short attrib;
 short rt1;
 short rt2;
 short aasamples;
 short pad4[3];
 GameDome dome;
 short stereoflag;
 short stereomode;
 float eyeseparation;
 RecastData recastData;
 float gravity;
 float activityBoxRadius;
 int flag;
 short mode;
 short matmode;
 short occlusionRes;
 short physicsEngine;
 short exitkey;
 short pad;
 short ticrate;
 short maxlogicstep;
 short physubstep;
 short maxphystep;
 short obstacleSimulation;
 short pad1;
 float levelHeight;
};

struct UnitSettings {
 float scale_length;
 char system;
 char system_rotation;
 short flag;
};

struct PhysicsSettings {
 float gravity[3];
 int flag;
 int quick_cache_step;
 int rt;
};

struct Scene {
 ID id;
 struct AnimData* adt;
 Object* camera;
 struct World* world;
 Scene* set;
 ListBase<Base> base;
 Base* basact;
 Object* obedit;
 float cursor[3];
 float twcent[3];
 float twmin[3];
 float twmax[3];
 int lay;
 int layact;
 int lay_updated;
 short flag;
 short use_nodes;
 struct bNodeTree* nodetree;
 struct Editing* ed;
 struct ToolSettings* toolsettings;
 struct SceneStats* stats;
 RenderData r;
 AudioData audio;
 ListBase<> markers;
 ListBase<> transform_spaces;
 void* sound_scene;
 void* sound_scene_handle;
 void* sound_scrub_handle;
 void* speaker_handles;
 void* fps_info;
 struct DagForest* theDag;
 short dagisvalid;
 short dagflags;
 short recalc;
 short pad6;
 int pad5;
 int active_keyingset;
 ListBase<> keyingsets;
 GameFraming framing;
 GameData gm;
 UnitSettings unit;
 struct bGPdata* gpd;
 PhysicsSettings physics_settings;
 struct MovieClip* clip;
 uint64_t customdata_mask;
 uint64_t customdata_mask_modal;
};
