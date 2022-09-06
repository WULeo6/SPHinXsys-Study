/**
 * @file 	taylor_green.cpp
 * @brief 	2D taylor_green vortex flow example.
 * @details This is the one of the basic test cases.
 * @author 	Chi Zhang and Xiangyu Hu
 */
/**
  * @brief 	SPHinXsys Library.
  */
#include "sphinxsys.h"
/**
 * @brief Namespace cite here.
 */
using namespace SPH;
/**
 * @brief Basic geometry parameters and numerical setup.
 */
Real DL = 1.0;					   /**< box length. */
Real DH = 1.0;					   /**< box height. */
Real resolution_ref = 1.0 / 100.0; /**< Global reference resolution. */
/** Domain bounds of the system. */
BoundingBox system_domain_bounds(Vec2d(0), Vec2d(DL, DH));
/**
 * @brief Material properties of the fluid.
 */
Real rho0_f = 1.0;					/**< Reference density of fluid. */
Real U_f = 1.0;						/**< Characteristic velocity. */
Real c_f = 10.0 * U_f;				/**< Reference sound speed. */
Real Re = 100;						/**< Reynolds number. */
Real mu_f = rho0_f * U_f * DL / Re; /**< Dynamics viscosity. */

/**
 * @brief 	Fluid body shape definition.
 */
class WaterBlock : public MultiPolygonShape
{
public:
	explicit WaterBlock(const std::string &shape_name) : MultiPolygonShape(shape_name)
	{
		/** Geometry definition. */
		std::vector<Vecd> water_block_shape;
		water_block_shape.push_back(Vecd(0.0, 0.0));
		water_block_shape.push_back(Vecd(0.0, DH));
		water_block_shape.push_back(Vecd(DL, DH));
		water_block_shape.push_back(Vecd(DL, 0.0));
		water_block_shape.push_back(Vecd(0.0, 0.0));
		multi_polygon_.addAPolygon(water_block_shape, ShapeBooleanOps::add);
	}
};
/**
 * application dependent initial condition 
 */
class TaylorGreenInitialCondition
	: public fluid_dynamics::FluidInitialCondition
{
public:
	explicit TaylorGreenInitialCondition(SPHBody &sph_body)
		: fluid_dynamics::FluidInitialCondition(sph_body){};

	void update(size_t index_i, Real dt)
	{
		/** initial velocity profile */
		vel_[index_i][0] = -cos(2.0 * Pi * pos_[index_i][0]) *
							 sin(2.0 * Pi * pos_[index_i][1]);
		vel_[index_i][1] = sin(2.0 * Pi * pos_[index_i][0]) *
							 cos(2.0 * Pi * pos_[index_i][1]);
	}
};
/**
 * @brief 	Main program starts here.
 */
int main(int ac, char *av[])
{
	/**
	 * @brief Build up -- a SPHSystem --
	 */
	SPHSystem sph_system(system_domain_bounds, resolution_ref);
	/** Tag for computation start with relaxed body fitted particles distribution. */
	sph_system.reload_particles_ = false;
	//handle command line arguments
	sph_system.handleCommandlineOptions(ac, av);
	IOEnvironment io_environment(sph_system);
	/**
	 * @brief create body, particle and material property.
	 */
	FluidBody water_block(sph_system, makeShared<WaterBlock>("WaterBody"));
	water_block.defineParticlesAndMaterial<FluidParticles, WeaklyCompressibleFluid>(rho0_f, c_f, mu_f);
	// Using relaxed particle distribution if needed
	sph_system.reload_particles_
		? water_block.generateParticles<ParticleGeneratorReload>(io_environment, water_block.getName())
		: water_block.generateParticles<ParticleGeneratorLattice>();
	/** topology */
	BodyRelationInner water_block_inner(water_block);
	/**
	 * @brief 	Define all numerical methods which are used in this case.
	 */
	/** Initial velocity field */
	SimpleDynamics<TaylorGreenInitialCondition> initial_condition(water_block);
	/**
	 * @brief 	Methods used for time stepping.
	 */
	/** Initialize particle acceleration. */
	SimpleDynamics<TimeStepInitialization> time_step_initialization(water_block);
	/** Periodic BCs in x direction. */
	PeriodicConditionUsingCellLinkedList periodic_condition_x(water_block, water_block.getBodyShapeBounds(), xAxis);
	/** Periodic BCs in y direction. */
	PeriodicConditionUsingCellLinkedList periodic_condition_y(water_block, water_block.getBodyShapeBounds(), yAxis);

	/**
	 * @brief 	Algorithms of fluid dynamics.
	 */
	/** Evaluation of density by summation approach. */
	NewInteractionDynamicsWithUpdate<fluid_dynamics::DensitySummationInner> update_density_by_summation(water_block_inner);
	/** Time step size without considering sound wave speed. */
	ReduceDynamics<fluid_dynamics::AdvectionTimeStepSize> get_fluid_advection_time_step_size(water_block, U_f);
	/** Time step size with considering sound wave speed. */
	ReduceDynamics<fluid_dynamics::AcousticTimeStepSize> get_fluid_time_step_size(water_block);
	/** Pressure relaxation algorithm by using verlet time stepping. */
	/** Here, we do not use Riemann solver for pressure as the flow is viscous. 
	  * The other reason is that we are using transport velocity formulation, 
	  * which will also introduce numerical dissipation slightly. */
	fluid_dynamics::PressureRelaxationInner pressure_relaxation(water_block_inner);
	fluid_dynamics::DensityRelaxationRiemannInner density_relaxation(water_block_inner);
	/** Computing viscous acceleration. */
	InteractionDynamics<fluid_dynamics::ViscousAccelerationInner> viscous_acceleration(water_block_inner);
	/** Impose transport velocity. */
	InteractionDynamics<fluid_dynamics::TransportVelocityCorrectionInner> transport_velocity_correction(water_block_inner);
	/**
	 * @brief Output.
	 */
	/** Output the body states. */
	BodyStatesRecordingToVtp body_states_recording(io_environment, sph_system.real_bodies_);
	/** Write the particle reload files. */
	ReloadParticleIO write_particle_reload_files(io_environment, {&water_block});
	/** Output the body states for restart simulation. */
	RestartIO restart_io(io_environment, sph_system.real_bodies_);
	/** Output the mechanical energy of fluid body. */
	RegressionTestEnsembleAveraged<BodyReducedQuantityRecording<ReduceDynamics<TotalMechanicalEnergy>>>
		write_total_mechanical_energy(io_environment, water_block);
	/** Output the maximum speed of the fluid body. */
	RegressionTestDynamicTimeWarping<BodyReducedQuantityRecording<ReduceDynamics<MaximumSpeed>>>
		write_maximum_speed(io_environment, water_block);
	/**
	 * @brief Setup geometry and initial conditions
	 */
	initial_condition.exec();
	sph_system.initializeSystemCellLinkedLists();
	periodic_condition_x.update_cell_linked_list_.parallel_exec();
	periodic_condition_y.update_cell_linked_list_.parallel_exec();
	sph_system.initializeSystemConfigurations();
	/**
	 * @brief The time stepping starts here.
	 */
	/** If the starting time is not zero, please setup the restart time step or read in restart states. */
	if (sph_system.restart_step_ != 0)
	{
		GlobalStaticVariables::physical_time_ = restart_io.readRestartFiles(sph_system.restart_step_);
		water_block.updateCellLinkedList();
		periodic_condition_x.update_cell_linked_list_.parallel_exec();
		periodic_condition_y.update_cell_linked_list_.parallel_exec();
		water_block_inner.updateConfiguration();
	}
	/** Output the start states of bodies. */
	body_states_recording.writeToFile(0);
	/** Output the mechanical energy of fluid. */
	write_total_mechanical_energy.writeToFile(0);
	/**
	 * @brief 	Basic parameters.
	 */
	size_t number_of_iterations = sph_system.restart_step_;
	int screen_output_interval = 100;
	int restart_output_interval = screen_output_interval * 10;
	Real end_time = 5.0;
	Real output_interval = 0.1;	 /**< Time stamps for output of body states. */
	Real dt = 0.0;		 /**< Default acoustic time step sizes. */
	/** statistics for computing CPU time. */
	tick_count t1 = tick_count::now();
	tick_count::interval_t interval;
	/**
	 * @brief 	Main loop starts here.
	 */
	while (GlobalStaticVariables::physical_time_ < end_time)
	{
		Real integration_time = 0.0;
		/** Integrate time (loop) until the next output time. */
		while (integration_time < output_interval)
		{
			/** Acceleration due to viscous force. */
			time_step_initialization.parallel_exec();
			Real Dt = get_fluid_advection_time_step_size.parallel_exec();
			update_density_by_summation.parallel_exec();
			viscous_acceleration.parallel_exec();
			transport_velocity_correction.parallel_exec(Dt);
			/** Dynamics including pressure relaxation. */
			Real relaxation_time = 0.0;
			while (relaxation_time < Dt)
			{
				//avoid possible smaller acoustic time step size for viscous flow
				dt = SMIN(get_fluid_time_step_size.parallel_exec(), Dt);
				relaxation_time += dt;
				integration_time += dt;
				pressure_relaxation.parallel_exec(dt);
				density_relaxation.parallel_exec(dt);
				GlobalStaticVariables::physical_time_ += dt;
			}

			if (number_of_iterations % screen_output_interval == 0)
			{
				std::cout << std::fixed << std::setprecision(9) << "N=" << number_of_iterations << "	Time = "
						  << GlobalStaticVariables::physical_time_
						  << "	Dt = " << Dt << "	dt = " << dt << "\n";

				if (number_of_iterations % restart_output_interval == 0)
				{
					restart_io.writeToFile(number_of_iterations);
				}
			}
			number_of_iterations++;

			/** Water block configuration and periodic condition. */
			periodic_condition_x.bounding_.parallel_exec();
			periodic_condition_y.bounding_.parallel_exec();
			water_block.updateCellLinkedList();
			periodic_condition_x.update_cell_linked_list_.parallel_exec();
			periodic_condition_y.update_cell_linked_list_.parallel_exec();
			water_block_inner.updateConfiguration();
		}

		tick_count t2 = tick_count::now();
		write_total_mechanical_energy.writeToFile(number_of_iterations);
		write_maximum_speed.writeToFile(number_of_iterations);
		body_states_recording.writeToFile();
		tick_count t3 = tick_count::now();
		interval += t3 - t2;
	}
	tick_count t4 = tick_count::now();

	tick_count::interval_t tt;
	tt = t4 - t1 - interval;
	std::cout << "Total wall time for computation: " << tt.seconds()
			  << " seconds." << std::endl;

	write_particle_reload_files.writeToFile();

	if (!sph_system.reload_particles_)
	{
		write_total_mechanical_energy.newResultTest();
		write_maximum_speed.newResultTest();
	}

	return 0;
}
