//==============================================================================
//  SDR++ source module wrapper for Fobos SDR API
//  V.T.
//  LGPL-2.1 or above LICENSE
//  23.07.2024
//==============================================================================

#include <utils/flog.h>
#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <gui/style.h>
#include <config.h>
#include <gui/smgui.h>
#include <fobos.h>
#include <gui/widgets/stepped_slider.h>

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "fobos_source",
    /* Description:     */ "Fobos SDR source module for SDR++",
    /* Author:          */ "V.T.",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

ConfigManager config;

const char* SAMPLING_MODES_STR = "RF\0(HF1+j*HF2))\0HF1\0HF2\0";
const char* CLOCK_MODES_STR = "Internal\0External 10 MHz\0";
#define DEFAULT_BUF_LEN (128 * 1024)
#define DEFAULT_BUFS_COUNT 32  
//==============================================================================
//==============================================================================
class FobosSourceModule : public ModuleManager::Instance 
{
public:
    //==========================================================================
    FobosSourceModule(std::string name) 
    {
        flog::info("FobosSourceModule {0}", name);

        _name = name;

        _handler.ctx = this;
        _handler.selectHandler = menuSelected;
        _handler.deselectHandler = menuDeselected;
        _handler.menuHandler = menuHandler;
        _handler.startHandler = start;
        _handler.stopHandler = stop;
        _handler.tuneHandler = tune;
        _handler.stream = &_stream;

        fobos_rx_get_api_info(_lib_version, _drv_version);
        flog::info("Fobos SDR API Info lib: {0} drv {1}", _lib_version, _drv_version);

        refresh();

        config.acquire();
        std::string serial = config.conf["device"];
        config.release();
        selectBySerial(serial);

        sigpath::sourceManager.registerSource("Fobos SDR", &_handler);
    }
    //==========================================================================
    ~FobosSourceModule() 
    {
        flog::info("~FobosSourceModule {0}");
        stop(this);
        fobos_rx_close(_dev);
        sigpath::sourceManager.unregisterSource("Fobos SDR");
    }
    //==========================================================================
    void postInit() 
    {
        flog::info("FobosSourceModule::postInit()");
        if (_sampling_mode)
        {
            flog::info("FobosSourceModule ui::waterfall.setCenterFrequency {0}", 0.5 * _sample_rate);
            gui::waterfall.setCenterFrequency(0.5 * _sample_rate);
            int64_t freq = gui::freqSelect.frequency - int64_t(_center_frequency - 0.5 * _sample_rate);
            if (freq > 0LL)
            {
                gui::freqSelect.setFrequency(freq);
            }
        }
        core::setInputSampleRate(_sample_rate);
    }
    //==========================================================================
    void enable() 
    {
        flog::info("FobosSourceModule::enable()");
        enabled = true;
    }
    //==========================================================================
    void disable() 
    {
        flog::info("FobosSourceModule::enable()");
        enabled = false;
    }
    //==========================================================================
    bool isEnabled() 
    {
        return enabled;
    }
    //==========================================================================
    void refresh() 
    {
        printf("=== refresh ===\n");
        _serials.clear();
        _serials_txt = "";
        char buf[1024] = { 0 };
        int count = fobos_rx_list_devices(buf);
        char* pserial = strtok(buf, " ");
        for (int i = 0; i < count; i++)
        {
            printf("   sn: %s\n", pserial);
            _serials_txt += pserial;
            _serials_txt += '\0';
            _serials.push_back(pserial);
            pserial = strtok(0, " ");
        }
        printf("===============\n");
    }
    //==========================================================================
    void selectFirst() 
    {
        selectBySerial("");
    }
    //==========================================================================
    void selectBySerial(std::string serial) 
    {
        flog::info("FobosSourceModule::selectBySerial() {0}", serial);
        if (_serials.size() == 0)
        {
            return;
        }
        _dev_idx = 0;
        if (serial.length() == 0)
        {
            _serial = _serials[0];
        }
        else
        {
            for (int i = 0; i < _serials.size(); i++) 
            {
                if (_serials[i] == serial) 
                {
                    _dev_idx = i;
                    _serial = _serials[i];
                    break;
                }
            }
        }
        if (_serial != "") 
        {
            config.acquire();
            config.conf["device"] = _serial;
            config.release(true);
        }
        //
        loadSettings();
        // open
        int result = fobos_rx_open(&_dev, _dev_idx);
        if (result != 0) 
        {
            flog::error("Unable to open Fobos SDR device", result);
            _serial = "";
            _dev = nullptr;
            return;
        }            
        // obtain board info
        result = fobos_rx_get_board_info(_dev, _hw_revision, _fw_version, _manufacturer, _product, _board_serial);       
        if (result != 0) 
        {
            flog::error("Unable to obtain devoce info, {0}", result);
            return;
        }
        else
        {
            flog::info("    hw_revision:  {0}", _hw_revision);
            flog::info("    fw_version:   {0}", _fw_version);
            flog::info("    manufacturer: {0}", _manufacturer);
            flog::info("    product:      {0}", _product);
            flog::info("    serial:       {0}", _board_serial);
        }         
        // obtain available sample rates
        unsigned int count;
        _sample_idx = 0;
        result = fobos_rx_get_samplerates(_dev, 0, &count);
        if ((result == 0) && (count > 0))
        {
            _sample_rates.resize(count);
            fobos_rx_get_samplerates(_dev, _sample_rates.data(), &count);
            if (_sample_rates[0]>_sample_rates[count - 1])
            {
                std::reverse(_sample_rates.begin(), _sample_rates.end());
            }
            _sample_rates_txt = "";
            for (size_t i = 0; i < _sample_rates.size(); i++) 
            {
                _sample_rates_txt+= std::to_string(_sample_rates[i] * 1E-6) + "MHz";
                _sample_rates_txt += '\0';
                if (_sample_rate == _sample_rates[i])
                {
                    _sample_idx = i;
                }
            }            
        }
        // close
        fobos_rx_close(_dev);
        _dev = nullptr;
    }
    //==========================================================================
private:
    void saveSettings()
    {
        flog::info("FobosSourceModule::saveSettings() {0}", _serial);
        if (_serial.size() == 0)
        {
            return;
        }
        config.acquire();
        config.conf["devices"][_serial]["sample_rate"] = _sample_rate;
        config.conf["devices"][_serial]["lna_gain"] = _lna_gain;
        config.conf["devices"][_serial]["vga_gain"] = _vga_gain;
        config.conf["devices"][_serial]["sampling_mode"] = _sampling_mode;
        config.conf["devices"][_serial]["clock_source"] = _clock_source;
        config.conf["devices"][_serial]["user_gpo"] = _user_gpo;
        config.release(true);
        config.save();
    }
    //==========================================================================
    void loadSettings()
    {
        flog::info("FobosSourceModule::loadSettings() {0}", _serial);
        if (_serial.size() == 0)
        {
            return;
        }
        if (config.conf["devices"][_serial].contains("sample_rate")) 
        {
            _sample_rate = config.conf["devices"][_serial]["sample_rate"];
        }
        if (config.conf["devices"][_serial].contains("lna_gain")) 
        {
            _lna_gain = config.conf["devices"][_serial]["lna_gain"];
        }
        if (config.conf["devices"][_serial].contains("vga_gain")) 
        {
            _vga_gain = config.conf["devices"][_serial]["vga_gain"];
        }
        if (config.conf["devices"][_serial].contains("sampling_mode")) 
        {
            _sampling_mode = config.conf["devices"][_serial]["sampling_mode"];
        }        
        if (config.conf["devices"][_serial].contains("clock_source")) 
        {
            _clock_source = config.conf["devices"][_serial]["clock_source"];
        } 
        if (config.conf["devices"][_serial].contains("user_gpo")) 
        {
            _user_gpo = config.conf["devices"][_serial]["user_gpo"];
        }
    } 
    //==========================================================================
    static void menuSelected(void* ctx) 
    {
        FobosSourceModule* _this = (FobosSourceModule*)ctx;
        core::setInputSampleRate(_this->_sample_rate);
        flog::info("FobosSourceModule {0}: Menu Select", _this->_serial);
    }
    //==========================================================================
    static void menuDeselected(void* ctx) 
    {
        FobosSourceModule* _this = (FobosSourceModule*)ctx;
        flog::info("FobosSourceModule '{0}': Menu Deselect", _this->_serial);
    }
    //==========================================================================
    static void start(void* ctx) 
    {
        FobosSourceModule* _this = (FobosSourceModule*)ctx;
        if (_this->_running) 
        { 
            return; 
        }
        flog::info("FobosSourceModule '{0}': Start", _this->_serial);
        // open
        int result = fobos_rx_open(&_this->_dev, _this->_dev_idx);
        if (result != 0) 
        {
            flog::error("Unable to open Fobos SDR device", result);
            _this->_serial = "";
            _this->_dev = nullptr;
            return;
        }            
        // apply initial parameters
        double actual;
        result = fobos_rx_set_frequency(_this->_dev, _this->_center_frequency, &actual);
        if (result != 0)
        {
            flog::error("fobos_rx_set_frequency - error {0}! ", fobos_rx_error_name(result));
        }
        else
        {
            flog::info("actual frequecy = {0}", actual);
        }

        result = fobos_rx_set_direct_sampling(_this->_dev, _this->_sampling_mode);
        if (result != 0)
        {
            flog::error("fobos_rx_set_direct_sampling - error {0}! ", fobos_rx_error_name(result));
        }

        result = fobos_rx_set_lna_gain(_this->_dev, _this->_lna_gain);
        if (result != 0)
        {
            flog::error("fobos_rx_set_lna_gain - error {0}! ", fobos_rx_error_name(result));
        }

        result = fobos_rx_set_vga_gain(_this->_dev, _this->_vga_gain);
        if (result != 0)
        {
            flog::error("fobos_rx_set_vga_gain - error {0}! ", fobos_rx_error_name(result));
        }

        result = fobos_rx_set_samplerate(_this->_dev, _this->_sample_rate, &actual);
        if (result != 0)
        {
            flog::error("fobos_rx_set_samplerate - error {0}! ", fobos_rx_error_name(result));
        }
        else
        {
            flog::info("actual samplerate = {0}", actual);
        } 

        result = fobos_rx_set_clk_source(_this->_dev, _this->_clock_source);
        if (result != 0)
        {
            flog::error("fobos_rx_set_clk_source - error {0}! ", fobos_rx_error_name(result));
        }    

        result = fobos_rx_set_user_gpo(_this->_dev, _this->_user_gpo);
        if (result != 0)
        {
            flog::error("fobos_rx_set_user_gpo - error {0}! ", fobos_rx_error_name(result));
        }
            
        // actually start
        if (!_this->_rx_async_thread.joinable())
        {
            _this->_rx_async_thread = std::thread(&FobosSourceModule::rx_async_thread_loop, _this);
        }
        _this->running = true;
    }
    //==========================================================================
    static void stop(void* ctx) 
    {
        FobosSourceModule* _this = (FobosSourceModule*)ctx;
        if (!_this->running)
        {
            return;
        }
        flog::info("FobosSourceModule {0}: Stop!", _this->_serial);
        if (_this->_rx_async_thread.joinable())
        {
            fobos_rx_cancel_async(_this->_dev);
            _this->_rx_async_thread.join();
        }
                // close
        fobos_rx_close(_this->_dev);
        _this->_dev = nullptr;
        _this->running = false;
    }
    //==========================================================================
    static void tune(double freq, void* ctx) 
    {
        FobosSourceModule* _this = (FobosSourceModule*)ctx;
        if (_this->_sampling_mode == 0)
        {
            double step = 100.0E3;
            double new_freq = round(freq / step) * step;
            if (new_freq < 50.0E6)
            {
                new_freq = 50.0E6;
            }
            flog::info("FobosSourceModule {0}: Tune {1} -> {2}", _this->_serial, freq, new_freq);
            if (_this->_dev)
            {
                double actual;
                fobos_rx_set_frequency(_this->_dev, new_freq, &actual);
            }
            _this->_center_frequency = new_freq;
            if (new_freq != freq)
            {
                gui::waterfall.setCenterFrequency(new_freq);
            }
        }
        else
        {
            flog::error("FobosSourceModule {0}: Tune {1} forbiden in direct sampling mode!", _this->_serial, freq);
            gui::waterfall.setCenterFrequency(0.5 * _this->_sample_rate);
        }        
    }
    //==========================================================================
    static void menuHandler(void* ctx) 
    {
        FobosSourceModule* _this = (FobosSourceModule*)ctx;
        if (_this->running) 
        {
            SmGui::BeginDisabled();
        }
        SmGui::ForceSync();        
        bool changed = false;
        if (SmGui::Combo(CONCAT("##_fobos_dev_sel_", _this->_name), &_this->_dev_idx, _this->_serials_txt.c_str())) 
        {
            _this->selectBySerial(_this->_serials[_this->_dev_idx]);
            core::setInputSampleRate(_this->_sample_rate);
        }

        SmGui::SameLine();
        SmGui::FillWidth();
        SmGui::ForceSync();
        if (SmGui::Button(CONCAT("Refresh##_fobos_refresh_", _this->_name))) 
        {
            _this->refresh();
            config.acquire();
            std::string serial = config.conf["device"];
            config.release();
            _this->selectBySerial(serial);
            core::setInputSampleRate(_this->_sample_rate);
        }

        if (_this->running) 
        {
            SmGui::EndDisabled(); 
        }

        SmGui::LeftLabel("API v.");
        SmGui::SameLine();
        SmGui::Text(_this->_lib_version);

        SmGui::LeftLabel("HW r.");
        SmGui::SameLine();
        SmGui::Text(_this->_hw_revision);

        SmGui::LeftLabel("FW v.");
        SmGui::SameLine();
        SmGui::Text(_this->_fw_version);

        SmGui::LeftLabel("Rate");
        SmGui::FillWidth();
        if (SmGui::Combo(CONCAT("##_fobos_sample_rate_", _this->_name), &_this->_sample_idx, _this->_sample_rates_txt.c_str())) 
        {
            _this->_sample_rate = _this->_sample_rates[_this->_sample_idx];
            if (_this->_dev)
            {
                double actual;
                int result = fobos_rx_set_samplerate(_this->_dev, _this->_sample_rate, &actual);
                if (result != 0)
                {
                    flog::error("fobos_rx_set_samplerate - error {0}! ", fobos_rx_error_name(result));
                }
                else
                {
                    flog::info("actual samplerate = {0}", actual);
                }
            }
            if (_this->_sampling_mode)
            {
                gui::waterfall.setCenterFrequency(0.5 * _this->_sample_rate);
            }
            else
            {
                gui::waterfall.setCenterFrequency(_this->_center_frequency);
            }
            core::setInputSampleRate(_this->_sample_rate);
            changed = true;
        }

        SmGui::LeftLabel("Input");
        SmGui::FillWidth();
        int _sampling_mode = _this->_sampling_mode;
        if (SmGui::Combo(CONCAT("##_fobos_input_", _this->_name), &_sampling_mode, SAMPLING_MODES_STR)) 
        {
            double shift = _this->_center_frequency - 0.5 * _this->_sample_rate;
            if ((_this->_sampling_mode == 0) && (_sampling_mode > 0))
            {
                gui::waterfall.setCenterFrequency(0.5 * _this->_sample_rate);
                gui::freqSelect.setFrequency(gui::freqSelect.frequency - shift);
                core::setInputSampleRate(_this->_sample_rate);
            }
            if (((_sampling_mode == 0) && (_this->_sampling_mode > 0)))
            {
                gui::waterfall.setCenterFrequency(_this->_center_frequency);
                gui::freqSelect.setFrequency(gui::freqSelect.frequency + shift);
                core::setInputSampleRate(_this->_sample_rate);
            }
            if (_this->_dev)
            {
                int result = fobos_rx_set_direct_sampling(_this->_dev, _sampling_mode);
                if (result != 0)
                {
                    flog::error("fobos_rx_set_direct_sampling - error {0}! ", fobos_rx_error_name(result));
                }
            }
            _this->_sampling_mode = _sampling_mode;
            changed = true;
        }

        double _center_frequency = _this->_center_frequency;       
        if (_this->_sampling_mode)
        {
            _center_frequency = 0.5 * _this->_sample_rate;
            SmGui::BeginDisabled();
        }
        SmGui::LeftLabel("Center");
        SmGui::FillWidth();
        char freq_txt[32];
        sprintf(freq_txt, "%f", _center_frequency);
        if (SmGui::InputText(CONCAT("##_fobos_center_freq_", _this->_name), freq_txt, sizeof(freq_txt))) 
        {
            if (_this->_sampling_mode == 0)
            {
                _center_frequency = atof(freq_txt);
                if (_center_frequency < 50.0E6) 
                {
                    _center_frequency = 50.0E6;
                }
                if (_this->_dev)
                {
                    double actual;
                    int result = fobos_rx_set_frequency(_this->_dev, _center_frequency, &actual);
                    if (result != 0)
                    {
                        flog::error("fobos_rx_set_samplerate - error {0}! ", fobos_rx_error_name(result));
                    }
                    else
                    {
                        flog::info("actual samplerate = {0}", actual);
                    }
                }
                _this->_center_frequency = _center_frequency;
                gui::waterfall.setCenterFrequency(_this->_center_frequency);
            }
            changed = true;
        }        

        SmGui::LeftLabel("LNA");
        SmGui::FillWidth();
        if (SmGui::SliderInt(CONCAT("##_fobos_lna_", _this->_name), &_this->_lna_gain, 0, 3, SmGui::FMT_STR_NONE))
        {
            if (_this->_dev)
            {
                int result = fobos_rx_set_lna_gain(_this->_dev, _this->_lna_gain);
                if (result != 0)
                {
                    flog::error("fobos_rx_set_lna_gain - error {0}! ", fobos_rx_error_name(result));
                }          
            }  
            changed = true;
        }

        SmGui::LeftLabel("VGA");
        SmGui::FillWidth();
        if (SmGui::SliderInt(CONCAT("##_fobos_vga_", _this->_name), &_this->_vga_gain, 0, 15, SmGui::FMT_STR_NONE))
        {
            if (_this->_dev)
            {
                int result = fobos_rx_set_vga_gain(_this->_dev, _this->_vga_gain);
                if (result != 0)
                {
                    flog::error("fobos_rx_set_vga_gain - error {0}! ", fobos_rx_error_name(result));
                }
            }
            changed = true;
        }

        if (_this->_sampling_mode)
        {
            SmGui::EndDisabled();
        }

        SmGui::LeftLabel("Clock");
        SmGui::FillWidth();
        if (SmGui::Combo(CONCAT("##_fobos_clock_", _this->_name), &_this->_clock_source, CLOCK_MODES_STR))
        {
            if (_this->_dev)
            {
                int result = fobos_rx_set_clk_source(_this->_dev, _this->_clock_source);
                if (result != 0)
                {
                    flog::error("fobos_rx_set_clk_source - error {0}! ", fobos_rx_error_name(result));
                }
            }
            changed = true;
        }
        SmGui::LeftLabel("GPO");
        SmGui::FillWidth();
        int new_gpo = _this->_user_gpo;
        for (int i = 0; i < 8; i++)
        {
            bool gpo = _this->_user_gpo & (1 << i);
            std:: string check_label = "##_fobos_gpo_"+std::to_string(i);
            SmGui::SameLine();
            if (SmGui::Checkbox(check_label.c_str(), &gpo))
            {
                if (gpo)
                {
                    new_gpo |= (1 << i);
                }
                else
                {
                    new_gpo &= ~(1<<(i));
                }
            }
        }
        if (new_gpo != _this->_user_gpo)
        {
            flog::info("GPO {0}", new_gpo);
            fobos_dev_t * _dev = _this->_dev;
            int result = 0;
            if (!_dev)
            {
                result = fobos_rx_open(&_dev, _this->_dev_idx);
                if (result != 0)
                {
                    flog::error("fobos_rx_open - error {0}! ", fobos_rx_error_name(result));
                }                
            }
            if (_dev)
            {
                result = fobos_rx_set_user_gpo(_dev, new_gpo);
                if (result != 0)
                {
                    flog::error("fobos_rx_set_user_gpo - error {0}! ", fobos_rx_error_name(result));
                }
            }
            if (!_this->_dev)
            {
                fobos_rx_close(_dev);
            }
            changed = true;
        }
        _this->_user_gpo = new_gpo;

        if (changed)
        {
            _this->saveSettings();
        }
    }
    //==========================================================================
    static void _rx_callback(float* buf, uint32_t buf_length, void* ctx)
    {
        FobosSourceModule* _this = (FobosSourceModule*)ctx;
        if (_this->_sampling_mode == 2)
        {
            int count = buf_length / 4;
            for (int i = 0; i < count; i++)
            {
                // re
                buf[i * 8 + 1] = 0.0f;             // im = 0

                buf[i * 8 + 2] = -buf[i * 8 + 2];  // re = -re
                buf[i * 8 + 3] = 0.0f;             // im = 0

                // re
                buf[i * 8 + 5] = 0.0f;             // im = 0

                buf[i * 8 + 6] = -buf[i * 8 + 6];  // re = -re
                buf[i * 8 + 7] = 0.0f;             // im = 0
            }
        }
        else    
        if (_this->_sampling_mode == 3)
        {
            int count = buf_length / 4;
            for (int i = 0; i < count; i++)
            {
                buf[i * 8 + 0] = buf[i * 8 + 1];   // re = im
                buf[i * 8 + 1] = 0.0f;             // im = 0

                buf[i * 8 + 2] = - buf[i * 8 + 3]; // re = -im
                buf[i * 8 + 3] = 0.0f;             // im = 0

                buf[i * 8 + 4] = buf[i * 8 + 5];   // re = im
                buf[i * 8 + 5] = 0.0f;             // im = 0

                buf[i * 8 + 6] = - buf[i * 8 + 7]; // re = -im
                buf[i * 8 + 7] = 0.0f;             // im = 0
            }
        }
        memcpy(_this->_stream.writeBuf, buf, buf_length * 2 * sizeof(float));
        if (!_this->_stream.swap(buf_length)) 
        { 
            flog::error("FobosSourceModule::_rx_callback() stream.swap error! ");
        }
    }
    //==========================================================================
    void rx_async_thread_loop(void)
    {
        flog::info("Fobos SDR rx_async_thread_loop started");
        int result = fobos_rx_read_async(_dev, &_rx_callback, this, DEFAULT_BUFS_COUNT, DEFAULT_BUF_LEN);
        flog::info("Fobos SDR rx_async_thread_loop done {0}", result);
        _running = false;
    }
    //==========================================================================
    std::string _name;
    fobos_dev_t* _dev = nullptr;
    int _dev_idx = 0;
    bool enabled = true;
    SourceManager::SourceHandler _handler;
    bool running = false;
    std::vector<std::string> _serials;
    std::string _serials_txt = "";
    std::string _serial = "";

    //radio settings
    double _sample_rate = 25.0E6;
    int _sample_idx = 0;
    double _center_frequency = 100.0E6;
    int _sampling_mode = 0;
    int _lna_gain = 0;
    int _vga_gain = 0;
    int _clock_source = 0;
    int _user_gpo = 0;

    // device info
    char _lib_version[32] = {0};
    char _drv_version[32] = {0};  
    char _hw_revision[32] = {0};
    char _fw_version[32] = {0};
    char _manufacturer[32] = {0};
    char _product[32] = {0};
    char _board_serial[32] = {0}; 

    // async api usage
    std::thread _rx_async_thread;
    bool _running = false;
    dsp::stream<dsp::complex_t> _stream;


    std::vector<double> _sample_rates;
    std::string _sample_rates_txt = "";
};
//==============================================================================
MOD_EXPORT void _INIT_() 
{
    json def = json({});
    def["devices"] = json({});
    def["device"] = "";
    config.setPath(core::args["root"].s() + "/fobos_config.json");
    config.load(def);
    config.enableAutoSave();
}
//==============================================================================
MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) 
{
    return new FobosSourceModule(name);
}
//==============================================================================
MOD_EXPORT void _DELETE_INSTANCE_(ModuleManager::Instance* instance) 
{
    delete (FobosSourceModule*)instance;
}
//==============================================================================
MOD_EXPORT void _END_() 
{
    config.disableAutoSave();
    config.save();
}
//==============================================================================