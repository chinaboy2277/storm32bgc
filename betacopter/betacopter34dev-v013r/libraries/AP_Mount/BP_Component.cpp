// -*- tab-width: 4; Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*-

#include <AP_HAL/AP_HAL.h>
#include "../ArduCopter/Copter.h"
#include "../ArduCopter/version.h"
#include <GCS_MAVLink/include/mavlink/v2.0/checksum.h>
#include "BP_Component.h"
#include <AP_Param/AP_Param.h>

const AP_Param::GroupInfo BP_Component::var_info[] = {
    // @Param: _RC_TRGT
    // @DisplayName: RC Target operating mode
    // @Description: Operating mode of the native STorM32 mount when in RC Targeting
    // @Values: 0:mount,1:rcin
    // @User: Standard
    AP_GROUPINFO("_RC_TRGT", 0, BP_Component, _rc_target_type, 0),

    // @Param: _HAS_PAN
    // @DisplayName: Pan mode
    // @Description: Return value of has_pan_control() for the native STorM32 mount
    // @Values: 0:false,1:true
    // @User: Standard
    AP_GROUPINFO("_HAS_PAN", 1, BP_Component, _has_pan_control, 0),

    // @Param: _RCIN_DZ
    // @DisplayName: RC Target operating mode
    // @Description: Operating mode of the native STorM32 mount when in RC Targeting
    // @Values: 0...50
    // @User: Standard
    AP_GROUPINFO("_RCIN_DZ", 2, BP_Component, _rc_target_pwm_deadzone, 5),

    AP_GROUPEND
};

extern const AP_HAL::HAL& hal;
extern Copter copter;



//******************************************************
// Interface friends to be used from outside
//******************************************************
// needs also
// * copter.letme_gcs_send_text(MAV_SEVERITY_INFO, _firmwaretext);
// * copter.letme_get_ekf_filter_status();
// * copter.letme_get_motors_armed();

// that's a bit clumsy, but all efforts to pass around classes did fail for some unknown reason(s)
// at least it works
// this is maybe not even that bad, not everything needs to be a class
//
// this has become somewhat of a mess now, need to see how to improve that,
// maybe all fields public members of BP_Component and keep only BP_Component *myself"?
// would not be very "encapsulated" however
// ideally this would be some sort of a general "module"
// needs to be cleaner, but how
//
struct tBPComponentData {
    volatile bool is_initialised;
    volatile bool camera_trigger_is_set;
    volatile bool set_servo_is_set;  //Servo can only be controlled by Mavlink or Mission, not very useful
    volatile uint16_t set_servo_channel;
    volatile uint16_t set_servo_pwm;
    volatile bool do_mount_control_is_set;
    volatile tBP_Angles mount_control_angles;
    volatile tBP_Angles mount_attitude_angles;
    volatile bool gcs_sendbanner;

    AP_HAL::UARTDriver *uart;
    BP_Component *myself;
};
static tBPComponentData _bp = {
    false,    false,    false, 0, 1500,    false, {},    {},    false,    nullptr,    nullptr
};


/// friend of BP_Component
bool BP_Component_is_initialised()
{
     return _bp.is_initialised;
}

/// friend of BP_Component
void BP_Component_set_trigger_picture()
{
    _bp.camera_trigger_is_set = true;
}

/// friend of BP_Component  //Servo can only be controlled by Mavlink or Mission, so not much useful
void BP_Component_set_servo(uint8_t channel, uint16_t pwm)
{
    _bp.set_servo_channel = channel;
    _bp.set_servo_pwm = pwm;
    _bp.set_servo_is_set = true;
}

/// friend of BP_Component
void BP_Component_set_mount_control_deg(float pitch_deg, float roll_deg, float yaw_deg)
{
    //convert from ArduPilot to STorM32 convention
    _bp.mount_control_angles.deg.pitch = -pitch_deg;
    _bp.mount_control_angles.deg.roll = roll_deg;
    _bp.mount_control_angles.deg.yaw = -yaw_deg;
    _bp.mount_control_angles.type = bpangles_deg;
    _bp.do_mount_control_is_set = true; //do last, should not matter, but who knows
}

/// friend of BP_Component
void BP_Component_set_mount_control_rad(float pitch_rad, float roll_rad, float yaw_rad)
{
    //convert from ArduPilot to STorM32 convention
    _bp.mount_control_angles.deg.pitch = -ToDeg(pitch_rad);
    _bp.mount_control_angles.deg.roll = ToDeg(roll_rad);
    _bp.mount_control_angles.deg.yaw = -ToDeg(yaw_rad);
    _bp.mount_control_angles.type = bpangles_deg;
    _bp.do_mount_control_is_set = true; //do last, should not matter, but who knows
}

/// friend of BP_Component
void BP_Component_set_mount_control_pwm(uint16_t pitch_pwm, uint16_t roll_pwm, uint16_t yaw_pwm)
{
    _bp.mount_control_angles.pwm.pitch = pitch_pwm;
    _bp.mount_control_angles.pwm.roll = roll_pwm;
    _bp.mount_control_angles.pwm.yaw = yaw_pwm;
    _bp.mount_control_angles.type = bpangles_pwm;
    _bp.do_mount_control_is_set = true; //do last, should not matter, but who knows
}

/// friend of BP_Component
void BP_Component_get_mount_attitude_rad(float* pitch_rad, float* roll_rad, float* yaw_rad)
{
   *pitch_rad = -ToRad(_bp.mount_attitude_angles.deg.pitch); //0.0f;
   *roll_rad = ToRad(_bp.mount_attitude_angles.deg.roll); //0.0f;
   *yaw_rad = -ToRad(_bp.mount_attitude_angles.deg.yaw); //0.0f;
}

/// friend of BP_Component
void BP_Component_get_mount_attitude_deg(float* pitch_deg, float* roll_deg, float* yaw_deg)
{
   *pitch_deg = -_bp.mount_attitude_angles.deg.pitch; //0.0f;
   *roll_deg = _bp.mount_attitude_angles.deg.roll; //0.0f;
   *yaw_deg = -_bp.mount_attitude_angles.deg.yaw; //0.0f;
}

/// friend of BP_Component
void BP_Component_set_gcs_sendbanner()
{
    _bp.gcs_sendbanner = true;
}

/// friend of BP_Component
enum BPRCTARGETTYPE BP_Component_get_mount_rctargettype()
{
    if (_bp.myself)
        if (_bp.myself->_rc_target_type == bprctarget_radionin) return bprctarget_radionin;

    return bprctarget_mount;
}

/// friend of BP_Component
bool BP_Component_get_mount_haspancontrol()
{
    if (_bp.myself)
        if (_bp.myself->_has_pan_control > 0) return true;

    return false;
}


void BP_Component_uart_write(const char* str)
{
//    _bp.uart->write(str);
//    _bp.myself->_uart->write(str); //then _uart needs to be public !!!!
}



//******************************************************
// BP_Component class functions
//******************************************************

//#define OWDEBUGFLOW
//#define OWDEBUGSEND

#define TASKFREQUENCY                   5  // this is the frequency of a task slice
#define TASKSLICES_NUMBER               10  // this is the number of task slices

#define TICKFREQUENCY                   (TASKFREQUENCY*TASKSLICES_NUMBER)
#define TICKPERIOD                      (400/TICKFREQUENCY)

#define FIND_COMPONENT_MAX_SEARCH_TIME  60000 //in ms

#define LIVEDATA_STATUS                 0x0001
#define LIVEDATA_TIMES                  0x0002
#define LIVEDATA_IMU1GYRO               0x0004
#define LIVEDATA_IMU1ACC                0x0008
#define LIVEDATA_IMU1R                  0x0010
#define LIVEDATA_IMU1ANGLES             0x0020
#define LIVEDATA_PIDCNTRL               0x0040
#define LIVEDATA_INPUTS                 0x0080
#define LIVEDATA_IMU2ANGLES             0x0100
#define LIVEDATA_MAGANGLES              0x0200
#define LIVEDATA_STORM32LINK            0x0400
#define LIVEDATA_IMUACCCONFIDENCE       0x0800
#define LIVEDATA_ATTITUDE_RELATIVE      0x1000

#define LIVEDATA_FLAGS                  (LIVEDATA_STATUS|LIVEDATA_ATTITUDE_RELATIVE)

typedef enum {
    STATUS_NOTINITIALIZED = 0,
    STATUS_FAILURE,
    STATUS_UART_INITIALIZED,
    STATUS_FOUND, //this way w can use >= for send_attitude()
    STATUS_READY
} STATUSTYPE;


/// Constructor
BP_Component::BP_Component(const AP_AHRS_TYPE &ahrs) :
    BP_STorM32(ahrs),
    _status(STATUS_NOTINITIALIZED),
    _tick_counter(0),
    _task_counter(0),
    _do_task(false)
{
    AP_Param::setup_object_defaults(this, var_info);

    //_bp = {};//superfluous, just to be absolutely sure 0 is really false
    _bp.is_initialised = false;
    _bp.camera_trigger_is_set = false;
    _bp.set_servo_is_set = false;
    _bp.do_mount_control_is_set = false;
    _bp.gcs_sendbanner = false;

    _bp.myself = this;
}

/// init
void BP_Component::init(const AP_SerialManager& serial_manager)
{
    _uart = serial_manager.find_serial(AP_SerialManager::SerialProtocol_STorM32_Native, 0);
    if (_uart) {
        _uart_is_initialised = true;
        receive_reset();
        _status = STATUS_UART_INITIALIZED;
        _bp.is_initialised = true; //tell it also to the outside
        _bp.uart = _uart; //to allow debug messages at any place
 //XX BP_Component_uart_write(THISFIRMWARE);
    }
}


//------------------------------------------------------
// main task handling
//------------------------------------------------------

/// do_tick
void BP_Component::do_tick()
{
    _tick_counter ++;
    if (_tick_counter>TICKPERIOD) { //this divides the 400Hz down to whatever is needed
        _tick_counter = 0;

        _task_counter ++;
        if (_task_counter>TASKSLICES_NUMBER) _task_counter = 0; //this slices the time into task slices

        if (_task_counter==0) {
            if (_status >= STATUS_FOUND) send_attitude();
        } else {
            _do_task = true; //postpone the other tasks until after the iNAV etc stuff
        }
    }
}

/// do_task
void BP_Component::do_task()
{
    if (!_uart_is_initialised) {
        return;
    }

    if (!_do_task) {
        return;
    }
    _do_task = false; //mark as done

    if (_status == STATUS_UART_INITIALIZED) { //branch off to find the gimbal
        find_gimbal();
        return;
    }

    switch (_task_counter) {
        case 1:case 6:
            // handle CMD_SETANGLES  @ 20Hz
            if (_bp.do_mount_control_is_set) {
                _bp.do_mount_control_is_set = false;
                if (_bp.mount_control_angles.type == bpangles_pwm) {
                    uint16_t pitch_pwm = _bp.mount_control_angles.pwm.pitch;
                    uint16_t roll_pwm = _bp.mount_control_angles.pwm.roll;
                    uint16_t yaw_pwm = _bp.mount_control_angles.pwm.yaw;

                    uint16_t DZ = _rc_target_pwm_deadzone;

                    if (pitch_pwm<10) pitch_pwm = 1500;
                    if (pitch_pwm < 1500-DZ) pitch_pwm += DZ; else if (pitch_pwm > 1500+DZ) pitch_pwm -= DZ; else pitch_pwm = 1500;

                    if (roll_pwm<10) roll_pwm = 1500;
                    if (roll_pwm < 1500-DZ) roll_pwm += DZ; else if (roll_pwm > 1500+DZ) roll_pwm -= DZ; else roll_pwm = 1500;

                    if (yaw_pwm<10) yaw_pwm = 1500;
                    if (yaw_pwm < 1500-DZ) yaw_pwm += DZ; else if (yaw_pwm > 1500+DZ) yaw_pwm -= DZ; else yaw_pwm = 1500;

                    send_cmd_setpitchrollyaw(pitch_pwm, roll_pwm, yaw_pwm);
                }else {
                    float pitch_deg = _bp.mount_control_angles.deg.pitch;
                    float roll_deg = _bp.mount_control_angles.deg.roll;
                    float yaw_deg = _bp.mount_control_angles.deg.yaw;

                    send_cmd_setangles(pitch_deg, roll_deg, yaw_deg, 0);
                }
            }
            break;
        case 2:case 7:
            // handle CMD_DOCAMERA  @ 20Hz
            if (_bp.camera_trigger_is_set) {
                _bp.camera_trigger_is_set = false;
                send_cmd_docamera(1);
            }
            break;
        case 3:
            // send GETDATA
            flush_rx(); //we are brutal and simply clear all incoming bytes
            receive_reset();
            send_cmd_getdatafields(LIVEDATA_FLAGS);
            break;
        case 4:case 5:
            // receive GETDATA response, give it two time slices, but it will receive only one message, and kill the rest
            do_receive();
            if (message_received() && (_serial.cmd == 0x06) && (_serial.getdatafields.flags == LIVEDATA_FLAGS)) {
                _datafields.livedata_status = _serial.getdatafields.livedata_status;
                //livedata_mavattitude is not saved, no use of it
                //mavattitude angles are send by STorM32 already converted to ArduPilot convention
                _bp.mount_attitude_angles.deg.pitch = _serial.getdatafields.livedata_attitude.pitch_deg;
                _bp.mount_attitude_angles.deg.roll = _serial.getdatafields.livedata_attitude.roll_deg;
                _bp.mount_attitude_angles.deg.yaw = _serial.getdatafields.livedata_attitude.yaw_deg;
            }
            break;
        case 8:
            // send CMD_SETINPUTS
            send_cmd_setinputs();
            break;
        case 9:
            // send stuff to GCS
            // here we want to inform the GCS, for that we would need to know if it is available or not!!!
            if (_bp.gcs_sendbanner) {
                _bp.gcs_sendbanner = false;
                copter.letme_gcs_send_text(MAV_SEVERITY_INFO, _firmwaretext);
            }
            break;
    }
}

///
void BP_Component::find_gimbal()
{
    if (!_uart_is_initialised) { //should never happen
        return;
    }

    // return if search time has passed
    if (AP_HAL::millis() > FIND_COMPONENT_MAX_SEARCH_TIME) {
        _status = STATUS_FAILURE;
        _uart_is_initialised = false;
        return;
    }

    switch (_task_counter) {
        case 7:
            // send GETVERSIONSTR
            flush_rx(); //we are brutal and simply clear all incoming bytes
            receive_reset();
            send_cmd_getversionstr();
            break;
        case 8:case 9:
            // receive GETVERSIONSTR response, give it two time slices, but it will receive only one message, and kill the rest
            do_receive();
            if (message_received() && (_serial.cmd==0x02)) {
                _status = STATUS_FOUND;
                receive_reset();
                uint16_t n, len = 0;
                char *s = _firmwaretext;
                strcpy(s, "STorM32 " ); len = 8;
                for (n=0;n<16;n++) { if (_serial.getversionstr.versionstr[n] == '\0') break; s[len++] = _serial.getversionstr.versionstr[n]; }
                s[len++] = ' ';
                for (n=0;n<16;n++) { if (_serial.getversionstr.boardstr[n] == '\0') break; s[len++] = _serial.getversionstr.boardstr[n]; }
                s[len++] = '\0';
            }
            break;
    }
}



//******************************************************
// BP_STorM32 class functions
//******************************************************

typedef enum {
    SERIALSTATE_IDLE = 0, //waits for something to come
    SERIALSTATE_RECEIVE_PAYLOAD_LEN,
    SERIALSTATE_RECEIVE_CMD,
    SERIALSTATE_RECEIVE_PAYLOAD,
    SERIALSTATE_MESSAGERECEIVED,
    SERIALSTATE_MESSAGERECEIVEDANDDIGESTED,
} SERIALSTATETYPE;


/// Constructor
BP_STorM32::BP_STorM32(const AP_AHRS_TYPE &ahrs) :
    _ahrs(ahrs),
    _uart(NULL),
    _uart_is_initialised(false), //_uart_is_initialised is a shorthand so to say for STATUS_NOTINITIALIZED or STATUS_FAILURE
    _storm32link_seq(0)
{
    _serial.state = SERIALSTATE_IDLE;
}

/// init
void BP_STorM32::set_uart(AP_HAL::UARTDriver *uart)
{
    _uart = uart;
    if (_uart) {
        _uart_is_initialised = true;
        _serial.state = SERIALSTATE_IDLE;
    }
}


//------------------------------------------------------
// send stuff
//------------------------------------------------------

///
// 24 bytes = 2084us @ 115200bps
void BP_STorM32::send_attitude()
{
    if (!_uart_is_initialised) {
        return;
    }

    if ((size_t)_uart->txspace() < sizeof(tSTorM32Link) +2) {
        return;
    }

#ifdef OWDEBUGSEND
    _uart->write( "Attitude" );
    return;
#endif

    Quaternion quat;
    quat.from_rotation_matrix( _ahrs.get_rotation_body_to_ned() );

    uint8_t status = 0;
    if (!_ahrs.initialised()) status |= 0x01;
    if (!_ahrs.healthy())     status |= 0x02;
    if (!copter.letme_get_ekf_filter_status()) status |= 0x04;

    if (copter.letme_get_motors_armed()) status |= 0x80;

    tSTorM32Link t;
    t.stx = 0xF9;
    t.len = 0x13;
    t.cmd = 0xD9;
    t.seq = _storm32link_seq; _storm32link_seq++;
    t.status = status;
    t.spare = 0;
    t.q1 = quat.q1;
    t.q2 = quat.q2;
    t.q3 = quat.q3;
    t.q4 = quat.q4;
    t.crc = crc_calculate(&(t.len), sizeof(tSTorM32Link)-3);

    _uart->write( (uint8_t*)(&t), sizeof(tSTorM32Link) );
}

///
// 19 bytes = 1650us @ 115200bps
void BP_STorM32::send_cmd_setangles(float pitch_deg, float roll_deg, float yaw_deg, uint16_t flags)
{
    if (!_uart_is_initialised) {
        return;
    }

    if ((size_t)_uart->txspace() < sizeof(tCmdSetAngles) +2) {
        return;
    }

#ifdef OWDEBUGSEND
    _uart->write( "Angels" );
    return;
#endif

    tCmdSetAngles t;
    t.stx = 0xF9; //0xFA; //0xF9 to suppress response
    t.len = 0x0E;
    t.cmd = 0x11;
    t.pitch = pitch_deg;
    t.roll = roll_deg;
    t.yaw = yaw_deg;
    t.flags = flags;
    t.type = 0;
    t.crc = crc_calculate(&(t.len), sizeof(tCmdSetAngles)-3);

    _uart->write( (uint8_t*)(&t), sizeof(tCmdSetAngles) );
}

///
// 11 bytes = 955us @ 115200bps
void BP_STorM32::send_cmd_setpitchrollyaw(uint16_t pitch, uint16_t roll, uint16_t yaw)
{
    if (!_uart_is_initialised) {
        return;
    }

    if ((size_t)_uart->txspace() < sizeof(tCmdSetPitchRollYaw) +2) {
        return;
    }

#ifdef OWDEBUGSEND
    _uart->write( "PitchRollYaw" );
    return;
#endif

    tCmdSetPitchRollYaw t;
    t.stx = 0xF9; //0xFA; //0xF9 to suppress response
    t.len = 0x06;
    t.cmd = 0x12;
    t.pitch = pitch;
    t.roll = roll;
    t.yaw = yaw;
    t.crc = crc_calculate(&(t.len), sizeof(tCmdSetPitchRollYaw)-3);

    _uart->write( (uint8_t*)(&t), sizeof(tCmdSetPitchRollYaw) );
}

///
// 11 bytes = 955us @ 115200bps
void BP_STorM32::send_cmd_docamera(uint16_t camera_cmd)
{
    if (!_uart_is_initialised) {
        return;
    }

    if ((size_t)_uart->txspace() < sizeof(tCmdDoCamera) +2) {
        return;
    }

#ifdef OWDEBUGSEND
    _uart->write( "Camera" );
    return;
#endif

    tCmdDoCamera t;
    t.stx = 0xF9; //0xFA; //0xF9 to suppress response
    t.len = 0x06;
    t.cmd = 0x0F;
    t.dummy1 = 0;
    t.camera_cmd = camera_cmd;
    t.dummy2 = 0;
    t.dummy3 = 0;
    t.dummy4 = 0;
    t.dummy5 = 0;
    t.crc = crc_calculate(&(t.len), sizeof(tCmdDoCamera)-3);

    _uart->write( (uint8_t*)(&t), sizeof(tCmdDoCamera) );
}

///
// 28 bytes = 2431us @ 115200bps
void BP_STorM32::send_cmd_setinputs()
{
    if (!_uart_is_initialised) {
        return;
    }

    if ((size_t)_uart->txspace() < sizeof(tCmdSetInputs) +2) {
        return;
    }

#ifdef OWDEBUGSEND
    _uart->write( "Inputs" );
    return;
#endif

    uint8_t status = 0;

    tCmdSetInputs t;
    t.stx = 0xF9; //0xFA; //0xF9 to suppress response
    t.len = 0x17;
    t.cmd = 0x16;
    t.channel0 = rcin_read(0);
    t.channel1 = rcin_read(1);
    t.channel2 = rcin_read(2);
    t.channel3 = rcin_read(3);
    t.channel4 = rcin_read(4);
    t.channel5 = rcin_read(5);
    t.channel6 = rcin_read(6);
    t.channel7 = rcin_read(7);
    t.channel8 = rcin_read(8);
    t.channel9 = rcin_read(9);
    t.channel10 = rcin_read(10);
    t.channel11 = rcin_read(11);
    t.channel12 = rcin_read(12);
    t.channel13 = rcin_read(13);
    t.channel14 = rcin_read(14);
    t.channel15 = rcin_read(15);
    t.status = status;
    t.crc = crc_calculate(&(t.len), sizeof(tCmdSetInputs)-3);

    _uart->write( (uint8_t*)(&t), sizeof(tCmdSetInputs) );
}

///
// 7 bytes = 608us @ 115200bps
void BP_STorM32::send_cmd_getdatafields(uint16_t flags)
{
    if (!_uart_is_initialised) {
        return;
    }

    if ((size_t)_uart->txspace() < sizeof(tCmdGetDataFields) +2) {
        return;
    }

#ifdef OWDEBUGSEND
    _uart->write( "Data" );
    return;
#endif

    tCmdGetDataFields t;
    t.stx = 0xF9; //0xFA; //0xF9 to suppress response
    t.len = 0x02;
    t.cmd = 0x06;
    t.flags = flags;
    t.crc = crc_calculate(&(t.len), sizeof(tCmdGetDataFields)-3);

    _uart->write( (uint8_t*)(&t), sizeof(tCmdGetDataFields) );
}

///
// 5 bytes = 434us @ 115200bps
void BP_STorM32::send_cmd_getversionstr()
{
    if (!_uart_is_initialised) {
        return;
    }

    if ((size_t)_uart->txspace() < sizeof(tCmdGetVersionStr) +2) {
        return;
    }

#ifdef OWDEBUGSEND
    _uart->write( "Version" );
    return;
#endif

    tCmdGetVersionStr t;
    t.stx = 0xF9; //0xFA; //0xF9 to suppress response
    t.len = 0x00;
    t.cmd = 0x02;
    t.crc = crc_calculate(&(t.len), sizeof(tCmdGetVersionStr)-3);

    _uart->write( (uint8_t*)(&t), sizeof(tCmdGetVersionStr) );
}


//------------------------------------------------------
// receive stuff
//------------------------------------------------------

///
inline void BP_STorM32::receive_reset()
{
    _serial.state = SERIALSTATE_IDLE;
}

/// reads in one char and processes it
// there should/could be some timeout handling
// there should/could be some error handling
// but flush_rx() will take care of both of that
void BP_STorM32::do_receive_singlechar()
{
    uint8_t c;

    if (_uart->available()<=0) return;  //this should never happen, but play it safe

    switch( _serial.state ){
        case SERIALSTATE_IDLE:
            c = _uart->read();

            if (c == 0xFB) { //the outcoming RCCMD start sign was received
                _serial.stx = c;
                _serial.state = SERIALSTATE_RECEIVE_PAYLOAD_LEN;
            }
            break;

        case SERIALSTATE_RECEIVE_PAYLOAD_LEN:
            c = _uart->read();

            _serial.len = c;
            _serial.state = SERIALSTATE_RECEIVE_CMD;
            break;

        case SERIALSTATE_RECEIVE_CMD:
            c = _uart->read();

            _serial.cmd = c;
            _serial.payload_cnt = 0;
            _serial.state = SERIALSTATE_RECEIVE_PAYLOAD;
            break;

        case SERIALSTATE_RECEIVE_PAYLOAD:
            c = _uart->read();

            if (_serial.payload_cnt>=SERIAL_RECEIVE_BUFFER_SIZE) {
                _serial.state = SERIALSTATE_IDLE; //error, get out of here
                return;
            }

            _serial.buf[_serial.payload_cnt++] = c;

            if (_serial.payload_cnt >= _serial.len+2 ){ //do expect always a crc
              uint16_t crc = 0; //XX ignore crc for the moment
              if (crc == 0) {
                  _serial.state = SERIALSTATE_MESSAGERECEIVED;
              }
            }
            break;

        case SERIALSTATE_MESSAGERECEIVED:case SERIALSTATE_MESSAGERECEIVEDANDDIGESTED:
            c = _uart->read();
            break;
    }
}

/// reads in as many chars as there are there
// one probably wants a protection that not too many messages are received,
// currently limits to just one message, and kills what's leftover
inline void BP_STorM32::do_receive()
{
    while (_uart->available()>0) {
        do_receive_singlechar();
    }

// serial state is reset by the flush_rx()
}

///
inline bool BP_STorM32::message_received()
{
    if (_serial.state == SERIALSTATE_MESSAGERECEIVED) {
        _serial.state = SERIALSTATE_MESSAGERECEIVEDANDDIGESTED;
        return true;
    }
    return false;
}


//------------------------------------------------------
// some helper functions
//------------------------------------------------------

///
inline void BP_STorM32::flush_rx()
{
    while( _uart->available()>0 ) _uart->read();
    _serial.state = SERIALSTATE_IDLE;
}


///
inline uint16_t BP_STorM32::rcin_read(uint8_t ch)
{
    uint16_t pulse = hal.rcin->read(ch);
    if (pulse<10) return 1500; else return pulse;
}









/*
ahrs.roll,
ahrs.pitch,
ahrs.yaw,

virtual const Matrix3f &get_rotation_body_to_ned(void) const = 0;

void AC_AttitudeControl::attitude_controller_run_quat(const Quaternion& att_target_quat, const Vector3f& att_target_ang_vel_rads)
{
    // Update euler attitude target and angular velocity target
    att_target_quat.to_euler(_att_target_euler_rad.x,_att_target_euler_rad.y,_att_target_euler_rad.z);
    _att_target_ang_vel_rads = att_target_ang_vel_rads;

    // Retrieve quaternion vehicle attitude
    // TODO add _ahrs.get_quaternion()
    Quaternion att_vehicle_quat;
    att_vehicle_quat.from_rotation_matrix(_ahrs.get_rotation_body_to_ned());
*/





/*
                //GCS_MAVLINK::send_statustext
                //GCS_MAVLINK::send_text(MAV_SEVERITY_INFO, _component_versionstr);

//                if (GCS_MAVLINK::initialised) {
//                  if (GCS_MAVLINK::is_initialised()) {

//                 mavlink_channel_t chan = (mavlink_channel_t)0;
//                GCS_MAVLINK::disable_channel_routing(chan);
//                    GCS_MAVLINK::send_text(MAV_SEVERITY_INFO, _component_firmwaretext);
//                 copter.letme_gcs_send_text(MAV_SEVERITY_INFO, _component_firmwaretext);
//                 Copter::gcs_send_text
//                }

 */
