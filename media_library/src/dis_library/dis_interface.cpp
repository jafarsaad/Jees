#include "dis_interface.h"

#include "dis.h"
#include "dis_math.h"
#include "camera.h"
#include "common.h"
#include "log.h"

#if LOG_TO_FILE   //see log.h
DisFileLog disFileLog;
#endif // LOG_TO_FILE

RetCodes dis_init(void** ctx,
               const char* cfg, int32_t cfg_bytes,
               const char* calib, int32_t calib_bytes,
               int32_t out_width, int32_t out_height,
               DewarpT* grid
              )
{
    if(grid == nullptr){
        LOGE("dis_init: grid = 0");
        return ERROR_INPUT_DATA;
    }
    //if ( grid->mesh_table != nullptr ) {
    //    LOGE("dis_init: grid->mesh_table already points to something");
    //    return ERROR_INPUT_DATA;
    //}
    if(*ctx != nullptr){
        return ERROR_CTX;  //*ctx alreay points to something
    }
    *ctx = new DIS;

    if(*ctx == nullptr){
        return ERROR_CTX;  //*ctx alreay points to something
    }
    DIS & dis = *reinterpret_cast<DIS*>(*ctx);

    LOG("dis_init cfg %d bytes, calib %d bytes, out resolution  %dx%d", cfg_bytes, calib_bytes, out_width, out_height);

    if( cfg[cfg_bytes-1] != 0 ) {
        LOGE("dis_init: Config string not terminated by 0 or improper cfg_bytes");
        return ERROR_CONFIG;
    }
    if( dis.parse_config( cfg ) ) return ERROR_CONFIG;  //fills dis.cfg

    if( calib[calib_bytes-1] != 0 ) {
        LOGE("dis_init: Calib string not terminated by 0 or improper calib_bytes");
        return ERROR_CALIB;
    }
    if( dis.init_in_cam( calib ) )  return ERROR_CALIB; //creates dis.in_cam

    RetCodes ret = dis.init(out_width, out_height);
    if( ret != DIS_OK )  return ret;

    //init grid structure: it tells the outer world what the grid will be
    grid->mesh_width  = 1+ (out_width  + MESH_CELL_SIZE_PIX -1 )/ MESH_CELL_SIZE_PIX; //ceil(width/cell)
    grid->mesh_height = 1+ (out_height + MESH_CELL_SIZE_PIX -1 )/ MESH_CELL_SIZE_PIX; //ceil(width/cell)

    dis.calc_out_rays(grid->mesh_width, grid->mesh_height, MESH_CELL_SIZE_PIX, NATURAL);

    return DIS_OK;
}

RetCodes dis_deinit( void** ctx )
{
    if(*ctx == nullptr){
        return ERROR_CTX;  //*ctx alreay points to something
    }
    DIS* pdis = reinterpret_cast<DIS*>(*ctx);

    delete pdis;

    *ctx = nullptr;

    return DIS_OK;
}

RetCodes dis_generate_grid( void* ctx,
                        int in_width, int in_height,
                        float motion_x, float motion_y,
                        int32_t panning,
                        FlipMirrorRot flip_mirror_rot,
                        DewarpT* grid )
{
    if(ctx == nullptr)
        return ERROR_CTX;  //*ctx alreay points to something
    if(grid == nullptr || grid->mesh_table == nullptr )
        return ERROR_GRID;  //grid not allocated

    DIS & dis = *reinterpret_cast<DIS*>(ctx);
    if (! dis.initialized)
        return ERROR_INIT;
    if ((in_width != dis.in_cam.res.x) || (in_height != dis.in_cam.res.y)){
        LOGE("dis_generateGrid: INput image resolutiuon differs from the one in the calibration");
        return ERROR_INPUT_DATA;
    }

    RetCodes ret = dis.generate_grid( vec2{motion_x, motion_y},  panning,  flip_mirror_rot,
                     *grid ); //output in grid->mesh_table[]

    return ret;
}

RetCodes dis_dewarp_only_grid(void* ctx,
                         int in_width, int in_height,
                         FlipMirrorRot flip_mirror_rot,
                         DewarpT* grid)
{
    if(ctx == nullptr)
        return ERROR_CTX;  //*ctx alreay points to something
    if(grid == nullptr || grid->mesh_table == nullptr )
        return ERROR_GRID;  //grid not allocated

    DIS & dis = *reinterpret_cast<DIS*>(ctx);
    if (! dis.initialized)
        return ERROR_INIT;
    if ((in_width != dis.in_cam.res.x) || (in_height != dis.in_cam.res.y)){
        LOGE("dis_generateGrid: INput image resolutiuon differs from the one in the calibration");
        return ERROR_INPUT_DATA;
    }

    dis.dewarp_only_grid(flip_mirror_rot, *grid);

    return DIS_OK;
}
